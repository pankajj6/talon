#pragma once
// hft_strategies.h // (In Development) ::

// All HFT strategy functions. Called from react.h based on agent.strategy field.
// Each function pushes OUCHs into reaction_queue.
// Kernel sets seq_num and schedules timestamps (timestamp = event.ts + l1 + l2).
//
// evo fill-flip mechanism : (in development)
//   Passive fill -> inventory acquisition -> window of optionality -> flip if F>R flow.
//   F>R = aggressor-side flow ratio signals toxic order flow.
//   HFT cancels resting orders on same side, flips to aggressor on opposite side.
//
// Spoof: place large visible order away from market, wait for retail reaction,
//   cancel before execution, profit from moved price.
//
// Quote stuffing (Nanex): flood book with cancels/replaces to slow competitors.

#include <deque>
#include <cstdint>
#include <cmath>
#include "events.h"
#include "agents.h"
#include "market_state.h"
#include "config.h"

extern uint64_t available_order_id;

// ============================================================
// HELPER: build a bare OUCH event shell (caller fills payload)
// ============================================================
inline Event make_ouch(uint64_t timestamp, Symbol sym, AgentTier tier, uint32_t idx, uint64_t causal)
{
    Event e;
    e.event_type        = EventType::OUCH;
    e.timestamp         = timestamp; // itch + l1 + l2 . passed by caller.
    e.symbol            = sym;
    e.agent_tier        = tier;
    e.agent_index       = idx;
    e.causal_parent_id  = causal;
    return e;
}

// ============================================================
// Cancel all tracked open orders for this HFT
// ============================================================
inline void cancel_all_open_orders(const Event& event , HFTAgent& agent, Symbol sym,
                                   std::deque<Event>& rq, uint64_t causal)
{
    uint64_t timestamp = event.timestamp + agent.l1 + agent.l2 ;
    for (uint8_t i = 0; i < agent.open_order_count; i++) {
        if (agent.open_order_ids[i] == 0) continue;
        Event e = make_ouch(timestamp , event.symbol, AgentTier::HFT, agent.id, causal);
        e.payload = OUCHPayload{ CancelOrder{ agent.open_order_ids[i], 0 }};
        rq.push_back(e);
    }
    agent.open_order_count = 0;
    for (auto& x : agent.open_order_ids) x = 0;
}

// ============================================================
// STRATEGY 0: PENNY JUMP
// Place bid one tick above best bid and ask one tick below best ask.
// Only if spread is wide enough to be profitable after fees.
// ADDED: cooldown check prevents spamming.
// ============================================================
inline void strategy_penny_jump(HFTAgent& agent, const Event& itch,
                                const MarketState& ms, std::deque<Event>& rq)
{
    if (ms.best_bid == 0 || ms.best_ask == 0) return;
    // ADDED: cooldown -- only react once per HFT_COOLDOWN_NS
    uint64_t agent_receive = itch.timestamp + agent.l1;
    // if (agent_receive - agent.last_react_ns < Config::HFT_COOLDOWN_NS) return;

    uint64_t spread = ms.spread;
    // need spread > 2*TICK + 2*taker_fee to be profitable // its mistake.
    if (spread < 3 * Config::TICK_SIZE) return; 

    // cancel stale orders before placing new ones
    cancel_all_open_orders(itch , agent, itch.symbol, rq, itch.sequence_num);

    uint64_t bid_price = ms.best_bid + agent.penny_jump_offset;
    uint64_t ask_price = ms.best_ask - agent.penny_jump_offset;
    if (bid_price >= ask_price) return; // crossed — skip

    uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;
    uint64_t bid_id = available_order_id++;
    Event bid_e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
    bid_e.payload = OUCHPayload{ EnterLimitOrder{ bid_id, bid_price, 100,
                                                  Order_Side::Buy, 0 }};
    rq.push_back(bid_e);

    uint64_t ask_id = available_order_id++;
    Event ask_e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
    ask_e.payload = OUCHPayload{ EnterLimitOrder{ ask_id, ask_price, 100,
                                                  Order_Side::Sell, 0 }};
    rq.push_back(ask_e);

    if (agent.open_order_count + 2 <= 16) {
        agent.open_order_ids[agent.open_order_count++] = bid_id;
        agent.open_order_ids[agent.open_order_count++] = ask_id;
    }
    agent.last_react_ns = agent_receive;
}

// ============================================================
// STRATEGY 5: MARKET MAKING (two-sided quoting)
// Post bid and ask simultaneously, earn the spread.
// Cancel and re-quote when price moves more than threshold.
// ============================================================
inline void strategy_market_make(HFTAgent& agent, const Event& itch,
                                 const MarketState& ms, std::deque<Event>& rq)
{
    // this check that freezes them.
    // if (ms.best_bid == 0 || ms.best_ask == 0) return; // mm needs to be brave , otherwise market is dead .

    uint64_t agent_receive = itch.timestamp + agent.l1;
    // if (agent_receive - agent.last_react_ns < Config::HFT_COOLDOWN_NS * 2) return;

    uint64_t spread = ms.spread;
    if (spread < 2 * Config::TICK_SIZE) return; // not profitable to make market

    cancel_all_open_orders(itch , agent, itch.symbol, rq, itch.sequence_num);

    if(std::abs(agent.inventory) < 600)
    {

        // If the book is empty, base the quote around the last known trade or fair price
        uint64_t my_bid = ms.best_bid > 0 ? ms.best_bid : ms.last_trade_price - Config::TICK_SIZE;
        uint64_t my_ask = ms.best_ask > 0 ? ms.best_ask : ms.last_trade_price + Config::TICK_SIZE;

        // quote at best to be at top of queue
        if (my_bid == 0 || my_ask == 0) return;

        uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;

        uint64_t bid_id = available_order_id++;
        Event bid_e = make_ouch(timestamp , itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
        bid_e.payload = OUCHPayload{ EnterLimitOrder{ bid_id, my_bid, 200,
                                                    Order_Side::Buy, 0 }};
        rq.push_back(bid_e);

        uint64_t ask_id = available_order_id++;
        Event ask_e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
        ask_e.payload = OUCHPayload{ EnterLimitOrder{ ask_id, my_ask, 200,
                                                    Order_Side::Sell, 0 }};
        rq.push_back(ask_e);
        if (agent.open_order_count + 2 <= 16) {
        agent.open_order_ids[agent.open_order_count++] = bid_id;
        agent.open_order_ids[agent.open_order_count++] = ask_id;
        }
        agent.last_react_ns = agent_receive;
        return;
    }

    if(agent.inventory > 600) // too much bought. sell 200 market order . other limit sell. we already canceled all orders. look if we can only cancel buy limit orders and double down on sell side.
    {
        uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;

        uint64_t oid = available_order_id++;
        Event e = make_ouch(timestamp ,itch.symbol , AgentTier::HFT, agent.id, itch.sequence_num);
        e.payload = OUCHPayload{EnterMarketOrder{oid , 200, Order_Side::Sell}};
        rq.push_back(e);

        uint64_t oid2 = available_order_id++;
        Event e2 = make_ouch(timestamp, itch.symbol , AgentTier::HFT, agent.id, itch.sequence_num);
        e2.payload = OUCHPayload{ EnterLimitOrder{ oid2, ms.best_ask - agent.penny_jump_offset, agent.inventory > 700 ? 300 : 200,
                                                  Order_Side::Sell, 0 }};
        rq.push_back(e2);
        if (agent.open_order_count + 2 <= 16) {
        agent.open_order_ids[agent.open_order_count++] = oid;
        agent.open_order_ids[agent.open_order_count++] = oid2;
        }
        agent.last_react_ns = agent_receive;
        return;
    }
    if(agent.inventory < -600) // too much short. buy 200 market order . other limit buy. we already canceled all orders. look if we can only cancel sell limit orders and double down on buy side.
    {
        uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;

        uint64_t oid = available_order_id++;
        Event e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
        e.payload = OUCHPayload{EnterMarketOrder{oid , 200, Order_Side::Buy}};
        rq.push_back(e);

        uint64_t oid2 = available_order_id++;
        Event e2 = make_ouch(timestamp , itch.symbol , AgentTier::HFT, agent.id, itch.sequence_num);
        e2.payload = OUCHPayload{ EnterLimitOrder{ oid2, ms.best_bid + agent.penny_jump_offset, agent.inventory > 700 ? 300 : 200,
                                                  Order_Side::Buy, 0 }};
        rq.push_back(e2);
        if (agent.open_order_count + 2 <= 16) {
            agent.open_order_ids[agent.open_order_count++] = oid;
            agent.open_order_ids[agent.open_order_count++] = oid2;
        }
        agent.last_react_ns = agent_receive;
        return;
    }
    
}


// ============================================================
// STRATEGY 1: JOIN AGGRESSOR
// When a large trade executes, assume momentum: join that direction
// with a limit order one tick inside the spread.
// ============================================================
inline void strategy_join_aggressor(HFTAgent& agent, const Event& itch,
                                    const MarketState& ms, std::deque<Event>& rq)
{
    if (ms.best_bid == 0 || ms.best_ask == 0) return;
    uint64_t agent_receive = itch.timestamp + agent.l1;
    // if (agent_receive - agent.last_react_ns < Config::HFT_COOLDOWN_NS) return;

    // only react to OrderExecuted ITCH
    auto* ip = std::get_if<ITCHPayload>(&itch.payload);
    if (!ip) return;
    auto* exec = std::get_if<OrderExecuted>(ip);
    if (!exec) return;

    // only react to large fills
    if (exec->executed_qty < 50) return;

    // determine direction from price vs mid
    // uint64_t mid = (ms.best_bid + ms.best_ask) / 2;
    // bool buy_side = (exec->price >= mid);


    cancel_all_open_orders(itch , agent, itch.symbol, rq, itch.sequence_num);

    bool buy_side = (exec->price == ms.old_best_ask); // check if buy side execution . join direction with market order.

    uint64_t price = buy_side
        ? ms.best_ask - agent.penny_jump_offset
        : ms.best_bid + agent.penny_jump_offset;

    uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;

    uint64_t oid = available_order_id++;
    Event e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
    if(exec->executed_qty <= 100) // if really large fill, be more aggressive and use market order to join immediately. otherwise use limit order to join passively.
    {
        e.payload = OUCHPayload{ EnterLimitOrder{ oid, price, 200,
                                 buy_side ? Order_Side::Buy : Order_Side::Sell, 0 }};
    }
    else
    {
        e.payload = OUCHPayload{ EnterMarketOrder{ oid, 200,
                                 buy_side ? Order_Side::Sell : Order_Side::Buy }};
        // later think if we can place a limit order 2 cent below this , if enough liqudity , and chance are more people join , so we sell at little high and get the aggressive flow at low.
    }
    rq.push_back(e);
    if (agent.open_order_count < 16)
        agent.open_order_ids[agent.open_order_count++] = oid;
    agent.last_react_ns = agent_receive;
}

// ============================================================
// STRATEGY 2: FILL-FLIP (evo43 mechanism)
// Phase 1 (ITCH): place passive limit order to acquire inventory.
// Phase 2 (SpecificOUCH fill received): detect if F>R flow is toxic.
//   If yes: cancel all same-side resting, flip to aggressor opposite.
// This is triggered from optional_react_specific(), not hft_react().
// ============================================================
inline void strategy_fill_flip_itch(HFTAgent& agent, const Event& itch,
                                    const MarketState& ms, std::deque<Event>& rq)
{
    if (ms.best_bid == 0 || ms.best_ask == 0) return;
    uint64_t agent_receive = itch.timestamp + agent.l1;
    // if (agent_receive - agent.last_react_ns < Config::HFT_COOLDOWN_NS) return;
    if (ms.spread < 2 * Config::TICK_SIZE) return;

    // intial market making . dont do it when imbalance is too high and you just got filled.. 
    if(std::abs(ms.order_imbalance) < Config::FILL_FLIP_FLOW_THRESHOLD ){ 
        strategy_market_make(agent, itch , ms , rq); 
        return;
    }

    // Place passive bid inside spread - wait to get hit
    uint64_t bid_price = ms.best_bid + Config::TICK_SIZE;
    if (bid_price >= ms.best_ask) return;

    uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;

    cancel_all_open_orders(itch, agent, itch.symbol, rq, itch.sequence_num);
    uint64_t oid = available_order_id++;
    Event e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
    e.payload = OUCHPayload{ EnterLimitOrder{ oid, bid_price, 100,
                                              Order_Side::Buy, 0 }};
    rq.push_back(e);
    if (agent.open_order_count < 16)
        agent.open_order_ids[agent.open_order_count++] = oid;
    agent.last_react_ns = agent_receive;
}

// Called from optional_react_specific when a FillNotification arrives
// This is where the actual flip decision happens.
inline void strategy_fill_flip_on_fill(const Event& event, HFTAgent& agent, const FillNotification& fill,
                                       const MarketState& ms, Symbol sym,
                                       std::deque<Event>& rq, uint64_t causal)
{
    // Record fill for flip window
    agent.fill_flip_inventory = agent.inventory; // post-fill inventory
    agent.fill_flip_price     = fill.price;
    agent.last_fill_time      = 0; // will be set by kernel from event timestamp
    agent.in_flip_window      = true;

    if (ms.best_bid == 0 || ms.best_ask == 0) return;
    uint64_t mid = (ms.best_bid + ms.best_ask) / 2;

    // F>R signal: if fill was on bid (we bought passively) and now mid is falling,
    // toxic flow -- flip: cancel bid side, sell aggressively
    bool we_bought = (fill.side == Order_Side::Buy);
    // avoid below checks . they are too rare.
    // bool price_adverse = we_bought ? (mid < fill.price) : (mid > fill.price);
    // if (!price_adverse) return; // no flip needed, hold position // i think here i need to check.....

    // Check order imbalance for F>R condition
    double imbalance = ms.order_imbalance;
    bool toxic = we_bought ? (imbalance < -Config::FILL_FLIP_FLOW_THRESHOLD)
                           : (imbalance >  Config::FILL_FLIP_FLOW_THRESHOLD);
    if (!toxic) return;

    //no below thing is maybe no right . cancel all orders , as market gonna move , maybe a cascade... 
            // // FLIP: cancel all resting same-side orders
           // this is not right , just cancel the orders that are opposite side limit orders.
            // for(int i = 0; i < hft.open_order_count; i++)
            // {
            //     if(hft.open_order_sides[i] == (we_bought ? 0 : 1)) // if same side as fill, cancel it.
            //     {
            //         Event e = make_ouch(sym, AgentTier::HFT, hft.id, causal);
            //         e.payload = OUCHPayload{ CancelOrder{ hft.open_order_ids[i], 0 }};
            //         rq.push_back(e);
            //         // remove from tracking
            //         // what this logic is not rigt maybe . i think in update on cancel , they need to do this .
            //         hft.open_order_ids[i] = hft.open_order_ids[--hft.open_order_count];
            //         hft.open_order_sides[i] = hft.open_order_sides[hft.open_order_count];
            //         hft.open_order_ids[hft.open_order_count] = 0;
            //     }
            // }
    //

    // this seems right , market may move very much now. so remove all your own orders in that direction.
    cancel_all_open_orders(event, agent, sym, rq, causal); 

    // Submit aggressor on opposite side to flatten
    int32_t flip_qty = (int32_t)std::abs(fill.filled_qty); // it is filled quantity that we offload.
    if (flip_qty < Config::FILL_FLIP_MIN_QTY) return;

    uint64_t timestamp = event.timestamp + agent.l1 + agent.l2 ;

    // Use market order to exit immediately (fill-flip is time-critical)
    uint64_t oid = available_order_id++;
    Event e = make_ouch(timestamp, event.symbol, AgentTier::HFT, agent.id, causal);
    e.payload = OUCHPayload{ EnterMarketOrder{
        oid, flip_qty,
        we_bought ? Order_Side::Sell : Order_Side::Buy
    }};
    rq.push_back(e);
    agent.in_flip_window = false;
    // check why dont we push open_order_ids here
}

// ============================================================
// STRATEGY 3: QUOTE STUFFING (Nanex mechanism)
// Flood the book with rapid orders+cancels to slow competitors.
// Only runs in bursts with interval between bursts.
// ============================================================
inline void strategy_quote_stuff(HFTAgent& agent, const Event& itch,
                                 const MarketState& ms, std::deque<Event>& rq)
{
    if (ms.best_bid == 0) return;
    uint64_t agent_receive = itch.timestamp + agent.l1;
    if (agent_receive - agent.last_stuff_time < Config::QUOTE_STUFF_INTERVAL_NS) return;

    // burst: place N limit orders deep in book then cancel them immediately
    uint64_t deep_bid = ms.best_bid - 10 * Config::TICK_SIZE;
    if (deep_bid == 0) return;

    uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;

    for (int i = 0; i < Config::QUOTE_STUFF_BURST && agent.open_order_count < 16; i++) {
        uint64_t oid = available_order_id++;
        Event place = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
        place.payload = OUCHPayload{ EnterLimitOrder{
            oid, deep_bid - (uint64_t)i * Config::TICK_SIZE,
            10, Order_Side::Buy, 0
        }};
        rq.push_back(place);

        // check if we need to increase open order count. what if cancel rejected sometime.

        // immediate cancel after placing (same reaction_queue, kernel schedules)
        Event cancel = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
        cancel.payload = OUCHPayload{ CancelOrder{ oid, 0 }};
        rq.push_back(cancel);
    }
    agent.last_stuff_time = agent_receive;
    agent.last_react_ns   = agent_receive;
}

// ============================================================
// STRATEGY 4: SPOOF + LAYERING
// Phase 1: place large visible order to pressure price direction.
// Phase 2 (after RETAIL_SLEEP_NS): cancel before fill.
// Phase 3: retail saw pressure, sent market orders, price moved -> profit.
// Retail scare strategy (RETAIL_STRAT_SCARE) complements this.
// ============================================================
inline void strategy_spoof_place(HFTAgent& agent, const Event& itch,
                                 const MarketState& ms, std::deque<Event>& rq)
{
    if (agent.has_spoof_order) return; // already have one active
    if (ms.best_bid == 0 || ms.best_ask == 0) return;
    uint64_t agent_receive = itch.timestamp + agent.l1;
    // if (agent_receive - agent.last_react_ns < Config::HFT_COOLDOWN_NS * 100) return;

    // Place large bid below best bid to signal buying pressure
    // Retail agents with SCARE or pressure logic will see order_imbalance spike
    uint64_t spoof_price = ms.best_bid - Config::SPOOF_PRICE_OFFSET; // i think we place spoof below bid to make sure not get exeuted .
    // if (spoof_price >= ms.best_ask) return; // dont cross spread

    uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;

    uint64_t oid = available_order_id++;
    Event e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
    e.payload = OUCHPayload{ EnterLimitOrder{
        oid, spoof_price, Config::SPOOF_ORDER_SIZE, Order_Side::Buy, 0
    }};
    rq.push_back(e);

    agent.spoof_order_id   = oid;
    agent.has_spoof_order  = true;
    agent.spoof_placed_time= agent_receive;
    agent.last_react_ns    = agent_receive;
}

// Called each ITCH tick while spoof is active, cancel when window expires
inline void strategy_spoof_cancel_check(HFTAgent& agent, const Event& itch,
                                        std::deque<Event>& rq)
{
    if (!agent.has_spoof_order) return;
    uint64_t agent_receive = itch.timestamp + agent.l1;
    if (agent_receive - agent.spoof_placed_time < Config::SPOOF_CANCEL_DELAY_NS) return;

    uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2 ;
    
    Event e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
    e.payload = OUCHPayload{ CancelOrder{ agent.spoof_order_id, 0 }};
    rq.push_back(e);
    agent.has_spoof_order = false;
    agent.spoof_order_id  = 0;
}




// test :

// ============================================================
// STRATEGY 99: THE BERSERKER (Pure Stress Test)
// Ignores all risk limits. Fires market orders continuously.
// ============================================================
inline void strategy_berserker(HFTAgent& agent, const Event& itch,
                               const MarketState& ms, std::deque<Event>& rq)
{
    // 1. NO COOLDOWNS. NO SPREAD CHECKS.
    
    // 2. Clear old tracking so we don't choke the 16-order limit
    cancel_all_open_orders(itch, agent, itch.symbol, rq, itch.sequence_num);

    // 3. Pseudo-random direction (Buy or Sell)
    bool buy_side = (itch.timestamp % 2 == 0); 

    // 4. Fire a 100-share Market Order directly into the book
    uint64_t timestamp = itch.timestamp + agent.l1 + agent.l2;
    uint64_t oid = available_order_id++;
    
    Event e = make_ouch(timestamp, itch.symbol, AgentTier::HFT, agent.id, itch.sequence_num);
    e.payload = OUCHPayload{ EnterMarketOrder{ oid, 100, buy_side ? Order_Side::Buy : Order_Side::Sell }};
    rq.push_back(e);

    // Track it so the engine clears it on fill/cancel
    if (agent.open_order_count < 16) {
        agent.open_order_ids[agent.open_order_count++] = oid;
    }
    
    agent.last_react_ns = itch.timestamp; // (Doesn't matter, we ignore cooldowns anyway)
}