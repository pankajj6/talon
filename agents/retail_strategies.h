#pragma once // in development
// retail_strategies.h
// Retail strategy functions. Called from react.h based on agent.strategy field.
// market order variants (50%+ retail use MO in reality)
// scare strategy: panic market order on large price drop
// pressure detection using order_imbalance (responds to spoofer)

#include <deque>
#include <cstdint>
#include <cmath>
#include "events.h"
#include "agents.h"
#include "broker_snapshot.h"
#include "market_state.h"
#include "config.h"

extern uint64_t available_order_id;

// ============================================================
// STRATEGY 0: TREND FOLLOW + LIMIT ORDER
// Buy limit at bid+1tick if price trending up, sell if down.
// ============================================================
inline void strategy_retail_trend_limit(RetailAgent& agent, uint64_t itch_ts,
                                        const BrokerSnapshot& snap,
                                        std::deque<Event>& rq)
{
    // recheck this logic , after some time..... important....
    if(agent.start_time > itch_ts) return; // not started yet. respect staggered start times.
    uint64_t agent_receive_ts = itch_ts + agent.l1;
    // this is important. without it retail floods the market with orders every second, which is unrealistic and also drowns out other strategies.
    if ((agent_receive_ts - agent.last_react_ns) < Config::RETAIL_SLEEP_NS) return;

    //start logic:
    if (snap.best_bid == 0 || snap.best_ask == 0) return;
    if (snap.prev_mid_price == 0) return;

    int64_t delta = (int64_t)snap.mid_price - (int64_t)snap.prev_mid_price;
    uint64_t abs_delta = (uint64_t)std::abs(delta);
    if (abs_delta < agent.trend_threshold) return;

    if (agent.has_open_order) {
        Event e;
        e.event_type       = EventType::OUCH;
        e.symbol           = Symbol::AAPL;
        e.agent_tier       = AgentTier::RETAIL;
        e.agent_index      = agent.id;
        e.payload = OUCHPayload{ CancelOrder{ agent.open_order_id, 0 }};
        rq.push_back(e);
        agent.has_open_order = false;
    }

    bool buy = (delta > 0);
    uint64_t price = buy
        ? (uint64_t)(snap.best_ask * agent.noise_factor)
        : (uint64_t)(snap.best_bid * agent.noise_factor);
    price = (price / Config::TICK_SIZE) * Config::TICK_SIZE;
    if (price == 0) return;

    uint64_t oid = available_order_id++;
    Event e;
    e.event_type       = EventType::OUCH;
    e.symbol           = Symbol::AAPL;
    e.agent_tier       = AgentTier::RETAIL;
    e.agent_index      = agent.id;
    e.payload = OUCHPayload{ EnterLimitOrder{ oid, price, 100,
                             buy ? Order_Side::Buy : Order_Side::Sell, 0 }};
    rq.push_back(e);
    agent.open_order_id  = oid;
    agent.has_open_order = true;
    // ADDED: update sleep cycle — only when actually submitting
    agent.last_react_ns  = itch_ts + agent.l1;
}

// ============================================================
// STRATEGY 1: TREND FOLLOW + MARKET ORDER
// majority of retail use market orders
// ============================================================
inline void strategy_retail_trend_market(RetailAgent& agent, uint64_t itch_ts,
                                         const BrokerSnapshot& snap,
                                         std::deque<Event>& rq)
{
    // recheck this logic , after some time..... important....
    if(agent.start_time > itch_ts) return; // not started yet. respect staggered start times.
    uint64_t agent_receive_ts = itch_ts + agent.l1;
    // this is important. without it retail floods the market with orders every second, which is unrealistic and also drowns out other strategies.
    if ((agent_receive_ts - agent.last_react_ns) < Config::RETAIL_SLEEP_NS) return;

    //start logic:
    if (snap.best_bid == 0 || snap.best_ask == 0) return;
    if (snap.prev_mid_price == 0) return;

    int64_t delta = (int64_t)snap.mid_price - (int64_t)snap.prev_mid_price;
    uint64_t abs_delta = (uint64_t)std::abs(delta);
    if (abs_delta < agent.trend_threshold) return;

    bool buy = (delta > 0);
    int32_t qty = (int32_t)(50 + (agent.noise_factor * 50)); // 50-100 shares

    uint64_t oid = available_order_id++;
    Event e;
    e.event_type       = EventType::OUCH;
    e.symbol           = Symbol::AAPL;
    e.agent_tier       = AgentTier::RETAIL;
    e.agent_index      = agent.id;
    e.payload = OUCHPayload{ EnterMarketOrder{ oid, qty,
                             buy ? Order_Side::Buy : Order_Side::Sell }};
    rq.push_back(e);
    agent.last_react_ns = itch_ts + agent.l1;
    // note: MO has no open_order_id to track (it fills immediately)
}

// ============================================================
// STRATEGY 2: MEAN REVERT + LIMIT ORDER
// Buy when price dropped below mean, sell when above
// ============================================================
inline void strategy_retail_mean_revert(RetailAgent& agent, uint64_t itch_ts,
                                        const BrokerSnapshot& snap,
                                        std::deque<Event>& rq)
{
    
    // recheck this logic , after some time..... important....
    if(agent.start_time > itch_ts) return; // not started yet. respect staggered start times.
    uint64_t agent_receive_ts = itch_ts + agent.l1;
    // this is important. without it retail floods the market with orders every second, which is unrealistic and also drowns out other strategies.
    if ((agent_receive_ts - agent.last_react_ns) < Config::RETAIL_SLEEP_NS) return;

    // start logic:
    if (snap.best_bid == 0 || snap.best_ask == 0) return;
    if (snap.prev_mid_price == 0) return;

    int64_t delta = (int64_t)snap.mid_price - (int64_t)snap.prev_mid_price;
    uint64_t abs_delta = (uint64_t)std::abs(delta);
    if (abs_delta < agent.trend_threshold) return;

    // opposite of trend: buy on down, sell on up
    bool buy = (delta < 0);
    uint64_t price = buy
        ? (uint64_t)(snap.best_bid * agent.noise_factor)
        : (uint64_t)(snap.best_ask * agent.noise_factor);
    price = (price / Config::TICK_SIZE) * Config::TICK_SIZE;
    if (price == 0) return;

    if (agent.has_open_order) {
        Event e;
        e.event_type = EventType::OUCH; e.symbol = Symbol::AAPL;
        e.agent_tier = AgentTier::RETAIL; e.agent_index = agent.id;
        e.payload = OUCHPayload{ CancelOrder{ agent.open_order_id, 0 }};
        rq.push_back(e);
        agent.has_open_order = false;
    }

    uint64_t oid = available_order_id++;
    Event e;
    e.event_type = EventType::OUCH; e.symbol = Symbol::AAPL;
    e.agent_tier = AgentTier::RETAIL; e.agent_index = agent.id;
    e.payload = OUCHPayload{ EnterLimitOrder{ oid, price, 100,
                             buy ? Order_Side::Buy : Order_Side::Sell, 0 }};
    rq.push_back(e);
    agent.open_order_id  = oid;
    agent.has_open_order = true;
    agent.last_react_ns  = itch_ts + agent.l1;
}

// ============================================================
// STRATEGY 3: NOISE TRADER
// Random direction limit order near mid. Creates baseline liquidity.
// ============================================================
inline void strategy_retail_noise(RetailAgent& agent, uint64_t itch_ts,
                                  const BrokerSnapshot& snap,
                                  std::deque<Event>& rq)
{


    // --- FAST DETERMINISTIC RANDOMIZER ---
    // Mix the Master Seed, the exact nanosecond, and the Agent ID
    uint64_t hash = Config::MASTER_SEED ^ itch_ts ^ agent.id;
    
    // Xorshift64* algorithm (Ultra-fast, perfectly reproducible)
    hash ^= hash >> 12;
    hash ^= hash << 25;
    hash ^= hash >> 27;
    hash *= 0x2545F4914F6CDD1DULL; 

    uint64_t pseudo_rand = hash % 100; 
    int32_t qty = 0;

    // The Liquidity Eater Logic
    if (pseudo_rand < 70) {
        qty = 50;  // 70% of the time: Small fry
    } else if (pseudo_rand < 95) {
        qty = 200; // 25% of the time: Standard
    } else {
        qty = 600; // 5% of the time: The Level Eater!
    }


    
    // recheck this logic , after some time..... important....
    if(agent.start_time > itch_ts) return; // not started yet. respect staggered start times.

    uint64_t agent_receive_ts = itch_ts + agent.l1;
    // this is important. without it retail floods the market with orders every second, which is unrealistic and also drowns out other strategies.
    if ((agent_receive_ts - agent.last_react_ns) < Config::RETAIL_SLEEP_NS) return;
    
    //start logic : 
    if (snap.mid_price == 0) return;

    // this is important. without it retail floods the market with orders every second, which is unrealistic and also drowns out other strategies.
    if ((agent_receive_ts - agent.last_react_ns) < Config::RETAIL_SLEEP_NS) return;

    // use agent id as quasi-random: odd=buy, even=sell
    bool buy = (agent.id % 2 == 0);
    uint64_t price = (uint64_t)(snap.mid_price * agent.noise_factor);
    price = (price / Config::TICK_SIZE) * Config::TICK_SIZE;
    if (price == 0) return;

    if (agent.has_open_order) {
        Event e;
        e.event_type = EventType::OUCH; e.symbol = Symbol::AAPL;
        e.agent_tier = AgentTier::RETAIL; e.agent_index = agent.id;
        e.payload = OUCHPayload{ CancelOrder{ agent.open_order_id, 0 }};
        rq.push_back(e);
        agent.has_open_order = false;
    }

    uint64_t oid = available_order_id++;
    Event e;
    e.event_type = EventType::OUCH; e.symbol = Symbol::AAPL;
    e.agent_tier = AgentTier::RETAIL; e.agent_index = agent.id;
    e.payload = OUCHPayload{ EnterMarketOrder{ oid, qty,
                             buy ? Order_Side::Buy : Order_Side::Sell }};
    rq.push_back(e);
    agent.open_order_id  = oid;
    agent.has_open_order = true;
    agent.last_react_ns  = agent_receive_ts;
}

// ============================================================
// STRATEGY 4: SCARE / PANIC
// ADDED: triggered when price drops > scare_price_threshold
// or when order_imbalance spikes (sees spoofer's large order)
// Sends market order to exit, amplifies price move.
// This complements spoofer strategy.
// ============================================================
inline void strategy_retail_scare(RetailAgent& agent, uint64_t itch_ts,
                                  const BrokerSnapshot& snap,
                                  const MarketState& ms,
                                  std::deque<Event>& rq)
{
    // recheck this logic , after some time..... important....
    if(agent.start_time > itch_ts) return; // not started yet. respect staggered start times.

    uint64_t agent_receive_ts = itch_ts + agent.l1;
    // this is important. without it retail floods the market with orders every second, which is unrealistic and also drowns out other strategies.
    if ((agent_receive_ts - agent.last_react_ns) < Config::RETAIL_SLEEP_NS) return;

    // now start logic: (agent awake).

    if (snap.best_bid == 0 || snap.mid_price == 0) return;

    int64_t delta = (int64_t)snap.mid_price - (int64_t)snap.prev_mid_price;

    // trigger: large price drop OR order_imbalance suddenly very high (spoofer visible)
    bool price_crash = (delta < 0 && (uint64_t)(-delta) > agent.scare_price_threshold);
    bool pressure_spike = (ms.order_imbalance > 0.7 || ms.order_imbalance < -0.7);
    if (!price_crash && !pressure_spike) return;

    // panic: cancel open order and send market order to exit position
    if (agent.has_open_order) {
        Event ec;
        ec.event_type = EventType::OUCH; ec.symbol = Symbol::AAPL;
        ec.agent_tier = AgentTier::RETAIL; ec.agent_index = agent.id;
        ec.payload = OUCHPayload{ CancelOrder{ agent.open_order_id, 0 }};
        rq.push_back(ec);
        agent.has_open_order = false;
    }

    if (agent.inventory == 0) return; // nothing to exit

    bool sell_to_exit = (agent.inventory > 0);
    int32_t qty = (int32_t)std::abs(agent.inventory);

    uint64_t oid = available_order_id++;
    Event e;
    e.event_type = EventType::OUCH; e.symbol = Symbol::AAPL;
    e.agent_tier = AgentTier::RETAIL; e.agent_index = agent.id;
    e.payload = OUCHPayload{ EnterMarketOrder{ oid, qty,
                sell_to_exit ? Order_Side::Sell : Order_Side::Buy }};
    rq.push_back(e);
    agent.last_react_ns = itch_ts + agent.l1;
}
