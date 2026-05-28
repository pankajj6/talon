#pragma once
#include "config.h"
#include "events.h" 
#include "lob.h"
#include <cstdint>
#include <variant>
#include <cmath>

struct PriceLevelSnapshot {
    uint64_t price;
    int64_t total_qty;
};

struct MarketState {
    Symbol symbol;
    uint64_t best_bid = 0;
    uint64_t best_ask = 0;
    uint64_t mid_price = Config::FAIR_PRICE_INITIAL;
    uint64_t old_best_bid = 0;
    uint64_t old_best_ask = 0;
    uint64_t spread = 0;
    int32_t bid_executed_shares = 0;
    int32_t ask_executed_shares = 0;
    uint32_t TotalVol = 0; 
    uint32_t last_trade_size = 0;
    
    uint64_t last_trade_price = 0; 
    double order_imbalance = 0; 
    int64_t Buy_liquidity = 0; 
    int64_t Sell_liquidity = 0; 

    PriceMap<true> bid_map{};
    PriceMap<false> ask_map{};

    uint8_t levels_captured = 0;

    // Momentum and Volatility Tracking
    uint64_t last_trade_ts = 0;
    double tape_bias = 0.0;
    double p_buy = 0.50;
    double current_variance = 0.000005; // Initial small variance // update to 5 times from last time.
    double current_sigma = 0.001;       // Initial small volatility
    

    MarketState() = default;
    MarketState(Symbol symbol_): symbol(symbol_) {}
};

inline void update_market_state(LOB& lob, Event& event, MarketState& M_State)
{
    if(event.event_type != EventType::ITCH) return; 
    if(M_State.symbol != event.symbol) return; 

    ITCHPayload itch_payload = std::get<ITCHPayload>(event.payload);

    M_State.old_best_ask = M_State.best_ask;
    M_State.old_best_bid = M_State.best_bid;

    if(auto* Added = std::get_if<OrderAdded>(&itch_payload)) {
        if(Added->side == Order_Side::Buy){
            M_State.Buy_liquidity += Added->quantity;
            M_State.bid_map.update_by_small_qty(Added->price, Added->quantity);
        } else {
            M_State.Sell_liquidity += Added->quantity;
            M_State.ask_map.update_by_small_qty(Added->price, Added->quantity);
        }
    }
    else if(auto* Cancel = std::get_if<OrderCancelled>(&itch_payload)) {
        if(Cancel->side == Order_Side::Buy){
            M_State.Buy_liquidity -= Cancel->cancelled_qty;
            M_State.bid_map.update_by_small_qty( Cancel->price , -(Cancel->cancelled_qty)); 
        } else {
            M_State.Sell_liquidity -= Cancel->cancelled_qty;
            M_State.ask_map.update_by_small_qty( Cancel->price , -(Cancel->cancelled_qty));
        }
    }
    else if(auto* replaced = std::get_if<OrderReplaced>(&itch_payload)) {
        int32_t Net_qty_added = replaced->new_added_qty - replaced->old_cancelled_qty;
        if(replaced->side == Order_Side::Buy) {
            M_State.Buy_liquidity += Net_qty_added; 
            M_State.bid_map.update_by_small_qty(replaced->new_price , replaced->new_added_qty); 
            M_State.bid_map.update_by_small_qty(replaced->old_price , -(replaced->old_cancelled_qty)); 
        } else { 
            M_State.Sell_liquidity += Net_qty_added; 
            M_State.ask_map.update_by_small_qty(replaced->new_price , replaced->new_added_qty); 
            M_State.ask_map.update_by_small_qty(replaced->old_price , -(replaced->old_cancelled_qty));
        }
    }

    
    else if(auto* executed = std::get_if<OrderExecuted>(&itch_payload)) {
        
        // 1. Update EWMA Volatility
        if (M_State.last_trade_price > 0 && executed->price > 0) {
            double p_curr = static_cast<double>(executed->price);
            double p_prev = static_cast<double>(M_State.last_trade_price);

            // CIRCUIT BREAKER: Cap the log return impact to +/- 1% per tick  -----------------------------------------------------------------------------------------
            double raw_log_return = std::log(p_curr / p_prev);
            double clamped_log_return = std::max(-0.01, std::min(0.01, raw_log_return));
            
            // double log_return = std::log(p_curr / p_prev);
            
            M_State.current_variance = (Config::VOLATILITY_LAMBDA * M_State.current_variance) + 
                                       ((1.0 - Config::VOLATILITY_LAMBDA) * clamped_log_return * clamped_log_return);
            M_State.current_sigma = std::sqrt(M_State.current_variance);
        }

        M_State.last_trade_price = executed->price;
        M_State.last_trade_size = executed->executed_qty;
        
        if(executed->side == Order_Side::Buy){
            // DECREMENT LIQUIDITY ON EXECUTION!
            M_State.Buy_liquidity -= executed->executed_qty; 
            M_State.bid_executed_shares += executed->executed_qty;
            M_State.bid_map.update_by_small_qty(executed->price , -(executed->executed_qty)); 
        } else { 
            // DECREMENT LIQUIDITY ON EXECUTION!
            M_State.Sell_liquidity -= executed->executed_qty; 
            M_State.ask_executed_shares += executed->executed_qty;
            M_State.ask_map.update_by_small_qty(executed->price , -(executed->executed_qty)); 
        }

        M_State.TotalVol += executed->executed_qty;
        M_State.order_imbalance = static_cast<double>(M_State.bid_executed_shares - M_State.ask_executed_shares) / M_State.TotalVol;

        // 2. Time Decay for Tape Bias
        double dt_seconds = static_cast<double>(event.timestamp - M_State.last_trade_ts) / 1e9;
        double decay_lambda = 0.5; 
        M_State.tape_bias *= std::exp(-decay_lambda * dt_seconds);

        M_State.last_trade_ts = event.timestamp;

        // 3. Volume Impact
        double volume_impact = Config::TAPE_VOLUME_SCALE * std::log1p(executed->executed_qty); 

        if (executed->side == Order_Side::Buy) {
            M_State.tape_bias += (0.50 - M_State.tape_bias) * volume_impact; 
        } else {
            M_State.tape_bias -= (M_State.tape_bias - (-0.50)) * volume_impact;
        }

        M_State.p_buy = 0.50 + M_State.tape_bias;
    }

    // ---------------------------------------------------------
    // THE GHOST LEVEL SCAN (Fixes the crossed book)
    // ---------------------------------------------------------
    uint64_t new_best_bid = 0;
    for (int i = 0; i < M_State.bid_map.end; ++i) {
        if (M_State.bid_map.level[i].price > 0 && M_State.bid_map.level[i].total_liquidity > 0) {
            new_best_bid = M_State.bid_map.level[i].price;
            break; 
        }
    }

    uint64_t new_best_ask = 0;
    for (int i = 0; i < M_State.ask_map.end; ++i) {
        if (M_State.ask_map.level[i].price > 0 && M_State.ask_map.level[i].total_liquidity > 0) {
            new_best_ask = M_State.ask_map.level[i].price;
            break; 
        }
    }

    // Safely calculate Midpoint 
    uint64_t midpoint = M_State.mid_price; 
    if (new_best_bid > 0 && new_best_ask > 0) {
        midpoint = (new_best_ask + new_best_bid) / 2;
        midpoint -= (midpoint % Config::TICK_SIZE); 
    } else if (new_best_bid > 0) {
        midpoint = new_best_bid;
    } else if (new_best_ask > 0) {
        midpoint = new_best_ask;
    }

    // Update Book parameters safely
    M_State.best_ask = new_best_ask;
    M_State.best_bid = new_best_bid;
    M_State.mid_price = midpoint; 
    M_State.spread = (new_best_ask > new_best_bid) ? (new_best_ask - new_best_bid) : 0;
}
    // else if(auto* executed = std::get_if<OrderExecuted>(&itch_payload)) {
        
    //     // 1. Update EWMA Volatility (current_sigma)
    //     if (M_State.last_trade_price > 0 && executed->price > 0) {
    //         double p_curr = static_cast<double>(executed->price);
    //         double p_prev = static_cast<double>(M_State.last_trade_price);
    //         double log_return = std::log(p_curr / p_prev);
            
    //         M_State.current_variance = (Config::VOLATILITY_LAMBDA * M_State.current_variance) + 
    //                                    ((1.0 - Config::VOLATILITY_LAMBDA) * log_return * log_return);
    //         M_State.current_sigma = std::sqrt(M_State.current_variance);
    //     }

    //     M_State.last_trade_price = executed->price;
    //     M_State.last_trade_size = executed->executed_qty;
        
    //     if(executed->side == Order_Side::Buy){
    //         M_State.bid_executed_shares += executed->executed_qty;
    //         M_State.bid_map.update_by_small_qty(executed->price , -(executed->executed_qty)); 
    //     } else { 
    //         M_State.ask_executed_shares += executed->executed_qty;
    //         M_State.ask_map.update_by_small_qty(executed->price , -(executed->executed_qty)); 
    //     }

    //     M_State.TotalVol += executed->executed_qty;
    //     M_State.order_imbalance = static_cast<double>(M_State.bid_executed_shares - M_State.ask_executed_shares) / M_State.TotalVol;

    //     // 2. Time Decay for Tape Bias
    //     double dt_seconds = static_cast<double>(event.timestamp - M_State.last_trade_ts) / 1e9;
    //     double decay_lambda = 0.5; 
    //     M_State.tape_bias *= std::exp(-decay_lambda * dt_seconds);

    //     M_State.last_trade_ts = event.timestamp;

    //     // 3. Volume Impact Asymptotic Math
    //     double volume_impact = Config::TAPE_VOLUME_SCALE * std::log1p(executed->executed_qty); 

    //     if (executed->side == Order_Side::Buy) {
    //         M_State.tape_bias += (0.50 - M_State.tape_bias) * volume_impact; 
    //     } else {
    //         M_State.tape_bias -= (M_State.tape_bias - (-0.50)) * volume_impact;
    //     }

    //     M_State.p_buy = 0.50 + M_State.tape_bias;
    // }


    // //  Safely check if the book has liquidity before accessing level[0]
    // uint64_t new_best_bid = (M_State.Buy_liquidity > 0) ? M_State.bid_map.level[0].price : 0; 
    // uint64_t new_best_ask = (M_State.Sell_liquidity > 0) ? M_State.ask_map.level[0].price : 0;

    // //  Safely calculate Midpoint without zero-collapsing
    // uint64_t midpoint = M_State.mid_price; // Default to old mid price
    // if (new_best_bid > 0 && new_best_ask > 0) {
    //     midpoint = (new_best_ask + new_best_bid) / 2;
    //     midpoint -= (midpoint % Config::TICK_SIZE); // Snap to tick
    // } else if (new_best_bid > 0) {
    //     midpoint = new_best_bid;
    // } else if (new_best_ask > 0) {
    //     midpoint = new_best_ask;
    // }

    // //  Update Book parameters safely
    // M_State.best_ask = new_best_ask;
    // M_State.best_bid = new_best_bid;
    // M_State.mid_price = midpoint; 
    // M_State.spread = (new_best_ask > new_best_bid) ? (new_best_ask - new_best_bid) : 0;


    // uint64_t new_best_bid = M_State.bid_map.level[0].price; 
    // uint64_t new_best_ask = M_State.ask_map.level[0].price;

    // uint64_t midpoint = (new_best_ask + new_best_bid)/2;
    // auto remdr = ( midpoint % Config::TICK_SIZE );
    // midpoint -= remdr;

    // M_State.best_ask = new_best_ask;
    // M_State.best_bid = new_best_bid;
    // M_State.mid_price = midpoint; 
    // M_State.spread = (new_best_ask - new_best_bid); 
// }