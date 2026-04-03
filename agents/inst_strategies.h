#pragma once
// (In Development)
// inst_strategies.h
// Institutional agent strategies. Large orders that move the market.
// TWAP slices over time, block executes when price is favorable.

#include <deque>
#include <cstdint>
#include "events.h"
#include "agents.h"
#include "market_state.h"
#include "config.h"

extern uint64_t available_order_id;

// ============================================================
// Block buy or sell: one large limit order at limit price
// Only executes once per agent (they have a target qty to fill)
// ============================================================
inline void strategy_inst_block(InstitutionalAgent& agent, uint64_t event_ts,
                                 const MarketState& ms, std::deque<Event>& rq)
{
    if (agent.remaining_quantity <= 0) return;
    if (ms.best_bid == 0 || ms.best_ask == 0) return;

    uint64_t agent_receive = event_ts + agent.l1;

    bool buy = (agent.direction == Order_Side::Buy);

    // only enter if price is within limit
    if (buy && ms.best_ask > agent.entry_limit_price) return;
    if (!buy && ms.best_bid < agent.entry_limit_price) return;

    int32_t qty = agent.remaining_quantity; // full block in one go
    
    // recheck here , logic is wrong i think
    uint64_t price = buy ? ms.best_bid + Config::TICK_SIZE : ms.best_ask - Config::TICK_SIZE; // at bid when want to buy , at ask when want to ask

    uint64_t oid = available_order_id++;
    Event e;
    e.event_type       = EventType::OUCH;
    e.symbol           = Symbol::AAPL;
    e.agent_tier       = AgentTier::INSTITUTIONAL;
    e.agent_index      = agent.id;
    e.causal_parent_id = 0;
    e.payload = OUCHPayload{ EnterLimitOrder{ oid, price, qty,
                             buy ? Order_Side::Buy : Order_Side::Sell, 0 }};
    rq.push_back(e);

    agent.remaining_quantity = 0; // submitted, done
    (void)agent_receive;
}

// ============================================================
// TWAP: time-weighted average price
// Slices into N equal orders spread over time
// ============================================================
inline void strategy_inst_twap(InstitutionalAgent& agent, uint64_t event_ts,
                                const MarketState& ms, std::deque<Event>& rq)
{
    if (agent.remaining_quantity <= 0) return;
    if (event_ts < agent.next_slice_time) return;
    if (ms.best_bid == 0 || ms.best_ask == 0) return;

    bool buy = (agent.direction == Order_Side::Buy);
    int32_t qty = (int32_t)std::min((uint64_t)agent.remaining_quantity, agent.slice_size);
    uint64_t price = buy
        ? ms.best_ask + Config::TICK_SIZE  // aggressive limit to ensure fill
        : ms.best_bid - Config::TICK_SIZE;

    uint64_t oid = available_order_id++;
    Event e;
    e.event_type       = EventType::OUCH;
    e.symbol           = Symbol::AAPL;
    e.agent_tier       = AgentTier::INSTITUTIONAL;
    e.agent_index      = agent.id;
    e.causal_parent_id = 0;
    e.payload = OUCHPayload{ EnterLimitOrder{ oid, price, qty,
                             buy ? Order_Side::Buy : Order_Side::Sell, 0 }};
    rq.push_back(e);

    agent.remaining_quantity -= qty;
    // schedule next slice
    agent.next_slice_time = event_ts + 5000000000ULL; // 5 seconds between slices
}
