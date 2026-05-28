#pragma once
#include "config.h"
#include "events.h" 
#include "lob.h"
#include <cstdint>
#include <variant>
// #include <pair>
// #include <map>


struct PriceLevelSnapshot {
    uint64_t price;
    int64_t total_qty;
};



struct MarketState {
    Symbol symbol;
    uint64_t best_bid = 0;
    uint64_t best_ask = 0;
    uint64_t mid_price = 0;
    uint64_t old_best_bid = 0;
    uint64_t old_best_ask = 0;
    uint64_t spread = 0;
    int32_t bid_executed_shares = 0;
    int32_t ask_executed_shares = 0;
    uint32_t TotalVol = 0; // total shares traded
    uint32_t last_trade_size = 0;
    
    // CHANGED: Initialize to Fair Price so it doesn't underflow!
    // uint64_t last_trade_price = Config::FAIR_PRICE_INITIAL; // recheck the logic . we should not do this . is zero not ok. done.
    uint64_t last_trade_price = 0; // zero is correct .
    double order_imbalance = 0; // (BidVol - AskVol) / (TotalVol)
    uint32_t Buy_liquidity = 0; // Book 
    uint32_t Sell_liquidity = 0; // Book

    // forget this now. we need to do custom .
    // PriceLevelSnapshot bids[10]; // Top 10 levels
    // PriceLevelSnapshot asks[10];

    PriceMap<true> bid_map ;
    PriceMap<false> ask_map;


    uint8_t levels_captured = 0;

    int volatility ; 

    MarketState() = default;
    MarketState(Symbol symbol_): symbol(symbol_) {
        symbol = symbol_;
    }
};


inline void update_market_state(LOB& lob, Event& event , MarketState& M_State)
{
    
     if(event.event_type != EventType::ITCH){
        return; // we only update using itch.
    }

    if(M_State.symbol != event.symbol){
        return; // sanity check: only update if symbol matches market state instance.
    }


    // so now i add the new logic here :
    
    // M_State.bid.update(price , new_qty);



    ITCHPayload itch_payload = std::get<ITCHPayload>(event.payload);

    M_State.old_best_ask = M_State.best_ask;
    M_State.old_best_bid = M_State.best_bid;
    // uint64_t new_best_ask = lob.best_ask(); /// this is mistake . kernel might be at a itch that is behind , and engie state changed becoz he processed and pushed all ths itch he generated .
    // the kernels job is to go and schedule the reaction to this things , if he just pulling the best prices from lob , then he is getting future best prices , while in past , at this itch , he should not get this ,
    // becoz while engine process the request , and matchs , the best prices transitions multiple best prices as more and more orders are getting executed the best prices  is getting transitioned and all . 
    //we need to consider that that kernel uses only this itch event he got to find the best price that existed the moment this itch was generated and the chnages happened to the book at that time. he just get info of that and that is only thing the agents should get. by this market state.
    //  now same mistake kernel did in that thing , pulling the lob.bid or lob.ask.levels direclty from that . those state of lob exist aftr the full ouch was processed by engine and not the state which is that should be at the time which is this whatever itch kernel is at , and 
    // as we know signle ouch request processed can generate multiple itchs and so  that is the reason multiple state of lob existed in between this full ouch processed and start of the processing , 
    // and which is why we now need to rebuild the book here, the kernel maintain a little lob here , just with the price and the total quantity only and not this in order_added ,
    // he adds the quantity to that price level in map if exist otherwise a  new price level struct he pushs. in order cancel he go to that price level decrese the total size . in orderreplace ,
    // he go to delete from old price level the quantity , and incrrment the new qty to new price level or same . in order_executed thing he go to remove that quantity from the particular price level. add functionality of updating the thing , with total liquidty when hitting zero , remove that level automatically . otherwise just maintain it. 
    // uint64_t new_best_bid = lob.best_bid();

    // this long issue is resolved now...


    // logic is now below . after updation of book:

    if( auto* Added = std::get_if<OrderAdded>(&itch_payload))
    {
        if(Added->side == Order_Side::Buy){
            M_State.Buy_liquidity += Added->quantity;
 
            M_State.bid_map.update_by_small_qty(Added->price, Added->quantity);
        }
        else {
            M_State.Sell_liquidity += Added->quantity;
            M_State.ask_map.update_by_small_qty(Added->price, Added->quantity);
        }


    }

    else if(auto* Cancel = std::get_if<OrderCancelled>(&itch_payload))
    {
        if(Cancel->side == Order_Side::Buy){
            M_State.Buy_liquidity -= Cancel->cancelled_qty;
            M_State.bid_map.update_by_small_qty( Cancel->price , -(Cancel->cancelled_qty)); // pass negative to decrement.
        }
        else {
            M_State.Sell_liquidity -= Cancel->cancelled_qty;
            M_State.ask_map.update_by_small_qty( Cancel->price , -(Cancel->cancelled_qty));
        }
    }

    else if(auto* replaced = std::get_if<OrderReplaced>(&itch_payload))
    {
        // can be negative
        int32_t Net_qty_added = replaced->new_added_qty - replaced->old_cancelled_qty;
        if(replaced->side == Order_Side::Buy) {
            M_State.Buy_liquidity += Net_qty_added  ; // update

            M_State.bid_map.update_by_small_qty(replaced->new_price , replaced->new_added_qty); // add new order qty. new level or old.
            M_State.bid_map.update_by_small_qty(replaced->old_price , -(replaced->old_cancelled_qty)); // decrement from old order level
        }
        else{ 
            M_State.Sell_liquidity += Net_qty_added; 
            M_State.ask_map.update_by_small_qty(replaced->new_price , replaced->new_added_qty); 
            M_State.ask_map.update_by_small_qty(replaced->old_price , -(replaced->old_cancelled_qty));
        }
    }
    
    // Update Volume parameters.
    else if(auto* executed = std::get_if<OrderExecuted>(&itch_payload))
    {
        M_State.last_trade_price = executed->price;
        M_State.last_trade_size = executed->executed_qty;
        
        if(executed->side == Order_Side::Buy){

            M_State.bid_executed_shares += executed->executed_qty;
            M_State.bid_map.update_by_small_qty(executed->price , -(executed->executed_qty)); // decrement the qty.
        }

        else { // trade at ask.

            M_State.ask_executed_shares += executed->executed_qty;
            M_State.ask_map.update_by_small_qty(executed->price , -(executed->executed_qty)); // decrement the qty.
        }

        // incr total volume.
        M_State.TotalVol += executed->executed_qty;
        // Adding static_cast<double> so C++ doesn't truncate it to 0!
        M_State.order_imbalance = static_cast<double>(M_State.bid_executed_shares - M_State.ask_executed_shares) / M_State.TotalVol;

    }

    
    // now update : 

    uint64_t new_best_bid = M_State.bid_map.level[0].price ; // now correct logic.
    uint64_t new_best_ask = M_State.ask_map.level[0].price ;

    uint64_t midpoint = (new_best_ask + new_best_bid)/2;

    auto remdr = ( midpoint % Config::TICK_SIZE ) ;
    midpoint -= remdr;

    // Update Book parameters:
    M_State.best_ask = new_best_ask;
    M_State.best_bid = new_best_bid;
    M_State.mid_price = midpoint; // 
    M_State.spread = (new_best_ask - new_best_bid); // in PRICE_SCALE


}





// old flawed code : (well idea behind it was flawed)
 // auto b = lob.copy_bid();
    // auto a = lob.copy_ask();

    // for(int i=0; i<10 ; i++)
    // {
    //     if(i < b.end){
    //         M_State.bids[i] = PriceLevelSnapshot{b.level[i].price , b.level[i].total_liquidity };
    //     }
    //     else{
    //         M_State.bids[i] = PriceLevelSnapshot{0,0};
    //     }

    //     if(i < a.end){
    //         M_State.asks[i] = PriceLevelSnapshot{a.level[i].price , a.level[i].total_liquidity};
    //     }
    //     else{
    //         M_State.asks[i] = PriceLevelSnapshot{0,0};
    //     }
    // }