#pragma once
// react.h
// Agent reaction functions.
// Each function pushes into reaction_queue if the agent decides to act.
// Kernel sets timestamps after. Agents never touch their own state here.

#include <deque>
#include <cstdint>
#include "events.h"
#include "agents.h"
#include "market_state.h"
#include "broker_snapshot.h"
#include "config.h"

extern uint64_t available_order_id; // from main.cpp

// ============================================================
// HFT: reacts to every ITCH — uses MarketState + raw ITCH event
// Simple strategies: join aggressor if spread wide, penny jump
// ============================================================
inline void hft_react(HFTAgent& agent, const Event& itch_event,
                      const MarketState& ms, std::deque<Event>& rq)
{
    if (!agent.is_active) return;
    if (ms.best_bid == 0 || ms.best_ask == 0) return;

    uint64_t spread = ms.spread;

    // Strategy 1: penny jump -  place bid one tick above best bid if spread is wide
    if (spread > agent.join_aggressor_threshold) {
        uint64_t bid_price = ms.best_bid + agent.penny_jump_offset;
        if (bid_price < ms.best_ask) { // stay passive
            Event ouch;
            ouch.event_type       = EventType::OUCH;
            ouch.symbol           = itch_event.symbol;
            ouch.agent_tier       = AgentTier::HFT;
            ouch.agent_index      = agent.id;
            ouch.causal_parent_id = itch_event.sequence_num;
            ouch.payload = OUCHPayload{ EnterLimitOrder{
                available_order_id++,
                bid_price,
                50,  // 50 shares
                Order_Side::Buy,
                0    // DAY
            }};
            rq.push_back(ouch);
        }

        // Also place ask one tick below best ask
        uint64_t ask_price = ms.best_ask - agent.penny_jump_offset;
        if (ask_price > ms.best_bid) {
            Event ouch;
            ouch.event_type       = EventType::OUCH;
            ouch.symbol           = itch_event.symbol;
            ouch.agent_tier       = AgentTier::HFT;
            ouch.agent_index      = agent.id;
            ouch.causal_parent_id = itch_event.sequence_num;
            ouch.payload = OUCHPayload{ EnterLimitOrder{
                available_order_id++,
                ask_price,
                50,
                Order_Side::Sell,
                0
            }};
            rq.push_back(ouch);
        }
    }
}

// ============================================================
// Retail: reacts only if sleep cycle elapsed.
// Gets BrokerSnapshot only, not raw ITCH.
// ADDED: last_react_ns update happens here when order submitted
// ============================================================
inline void retail_react(RetailAgent& agent, uint64_t itch_timestamp,
                         const BrokerSnapshot& snap, std::deque<Event>& rq)
{
    if (!agent.is_active) return;
    if (snap.best_bid == 0 || snap.best_ask == 0) return;

    // ADDED: sleep cycle check -  agent eligible only if enough time passed
    // agent receives ITCH at itch_timestamp + l1
    uint64_t agent_receive_time = itch_timestamp + agent.l1;
    if (agent_receive_time - agent.last_react_ns < Config::RETAIL_SLEEP_NS) return;

    // Simple ZIP: compare current mid to previous mid
    if (snap.mid_price == 0 || snap.prev_mid_price == 0) return;

    int64_t delta = (int64_t)snap.mid_price - (int64_t)snap.prev_mid_price;
    uint64_t abs_delta = delta < 0 ? (uint64_t)(-delta) : (uint64_t)delta;

    if (abs_delta < agent.trend_threshold) return; // not enough movement

    // If trend follower: buy on up, sell on down
    // If mean reverter: buy on down, sell on up
    bool buy_signal;
    if (agent.is_trend_follower) buy_signal = (delta > 0);
    else                         buy_signal = (delta < 0);

    // Cancel existing open order first if they have one
    if (agent.has_open_order) {
        Event cancel;
        cancel.event_type       = EventType::OUCH;
        cancel.symbol           = Symbol::AAPL; // single symbol for now
        cancel.agent_tier       = AgentTier::RETAIL;
        cancel.agent_index      = agent.id;
        cancel.payload = OUCHPayload{ CancelOrder{ agent.open_order_id, 0 }};
        rq.push_back(cancel);
        agent.has_open_order = false;
    }

    // Place new limit order near the spread
    uint64_t price = buy_signal
        ? (uint64_t)(snap.best_bid * agent.noise_factor)
        : (uint64_t)(snap.best_ask * agent.noise_factor);

    // snap to tick
    price = (price / Config::TICK_SIZE) * Config::TICK_SIZE;
    if (price == 0) return;

    uint64_t oid = available_order_id++;
    Event ouch;
    ouch.event_type       = EventType::OUCH;
    ouch.symbol           = Symbol::AAPL;
    ouch.agent_tier       = AgentTier::RETAIL;
    ouch.agent_index      = agent.id;
    ouch.causal_parent_id = 0;
    ouch.payload = OUCHPayload{ EnterLimitOrder{
        oid,
        price,
        100, // 100 shares
        buy_signal ? Order_Side::Buy : Order_Side::Sell,
        0
    }};
    rq.push_back(ouch);

    // ADDED: update sleep timer to when agent received this ITCH
    agent.last_react_ns  = agent_receive_time;
    agent.open_order_id  = oid;
    agent.has_open_order = true;
}

// ============================================================
// Fundamentalist: reacts to fair price update event.
// Compares fair price to current market mid, corrects if too far.
// ============================================================
inline void fund_react(FundamentalistAgent& agent, uint64_t fair_price,
                       const MarketState& ms, std::deque<Event>& rq,
                       uint64_t event_seq)
{
    if (!agent.is_active) return;
    if (ms.best_bid == 0 || ms.best_ask == 0) return;

    uint64_t mid = (ms.best_bid + ms.best_ask) / 2;
    int64_t  dev = (int64_t)fair_price - (int64_t)mid;
    uint64_t abs_dev = dev < 0 ? (uint64_t)(-dev) : (uint64_t)dev;

    if (abs_dev < agent.correction_threshold) return;

    // If fair > mid: market is underpriced -> buy aggressively
    // If fair < mid: market is overpriced  -> sell aggressively
    bool buy = (dev > 0);

    // Use market order to correct immediately
    Event ouch;
    ouch.event_type       = EventType::OUCH;
    ouch.symbol           = Symbol::AAPL;
    ouch.agent_tier       = AgentTier::HFT; // fundamentalist uses HFT tier for fast processing
    ouch.agent_index      = agent.id;
    ouch.causal_parent_id = event_seq;
    ouch.payload = OUCHPayload{ EnterMarketOrder{
        available_order_id++,
        agent.order_size,
       buy ? Order_Side::Buy : Order_Side::Sell
    }};
    rq.push_back(ouch);
}