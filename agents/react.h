#pragma once

// react.h

// (In Development)



// Kernel calls these functions to let agents react to events.
// Each function dispatches to the appropriate strategy based on agent.strategy field.
// Kernel handles timestamps , agents never set their own.

#include <deque>
#include <cstdint>
#include "events.h"
#include "agents.h"
#include "market_state.h"
#include "broker_snapshot.h"
#include "config.h"
#include "hft_strategies.h"
#include "retail_strategies.h"
#include "inst_strategies.h"

extern uint64_t available_order_id;

// ============================================================
// HFT react : called for every ITCH event
// Dispatches to strategy function based on agent.strategy field
// ============================================================
inline void hft_react(HFTAgent& agent, const Event& itch,
                      const MarketState& ms, std::deque<Event>& rq)
{

    if (!agent.is_active) return;
    if (ms.best_bid == 0 || ms.best_ask == 0) return;

    // ADDED: The HFT Cooldown Shield!
    uint64_t agent_receive = itch.timestamp + agent.l1;
    // if (agent_receive - agent.last_react_ns < Config::HFT_COOLDOWN_NS) return;

    agent.last_react_ns = agent_receive;

    switch (agent.strategy) {
        case Config::HFT_STRAT_PENNY_JUMP:
            strategy_penny_jump(agent, itch, ms, rq);
            break;
        case Config::HFT_STRAT_JOIN_AGGRESSOR:
            strategy_join_aggressor(agent, itch, ms, rq);
            break;
        case Config::HFT_STRAT_FILL_FLIP:
            strategy_fill_flip_itch(agent, itch, ms, rq);
            break;
        case Config::HFT_STRAT_QUOTE_STUFF:
            strategy_quote_stuff(agent, itch, ms, rq);
            break;
        case Config::HFT_STRAT_SPOOF:
            strategy_spoof_place(agent, itch, ms, rq);
            strategy_spoof_cancel_check(agent, itch, rq);
            break;
        case Config::HFT_STRAT_MARKET_MAKE:
            strategy_market_make(agent, itch, ms, rq);
            break;
        case Config::HFT_STRAT_BERSERKER:
            strategy_berserker(agent, itch, ms, rq);
            break;
        default:
            strategy_penny_jump(agent, itch, ms, rq);
            break;
    }

    // ADDED: adaptive strategy switch for agents with can_switch_strategy
    // If inventory too large, switch to market making to offload
    if (agent.can_switch_strategy && std::abs(agent.inventory) > 500) {
        agent.strategy = Config::HFT_STRAT_MARKET_MAKE;
    }
}

// ============================================================
// HFT optional_react_specific:  called on SPECIFIC_OUCH (fill, cancel etc)
// This is where fill-flip Phase 2 happens
// ============================================================
inline void hft_react_specific(HFTAgent& agent, const Event& sp_event,
                               const MarketState& ms, Symbol sym,
                               std::deque<Event>& rq)
{
    if (!agent.is_active) return;
    auto* sp = std::get_if<SpecificOUCHPayload>(&sp_event.payload);
    if (!sp) return;

    if (auto* fill = std::get_if<FillNotification>(sp)) {
        // ADDED: track open orders -  remove filled order from array
        for (uint8_t i = 0; i < agent.open_order_count; i++) {
            if (agent.open_order_ids[i] == fill->order_id) {
                agent.open_order_ids[i] = agent.open_order_ids[--agent.open_order_count];
                agent.open_order_ids[agent.open_order_count] = 0;
                break;
            }
        }
        // fill-flip phase 2 -  only for fill-flip strategy agents
        if (agent.strategy == Config::HFT_STRAT_FILL_FLIP) {
            strategy_fill_flip_on_fill(sp_event,agent, *fill, ms, sym, rq,
                                       sp_event.sequence_num);
        }
    }
    else if (auto* resting = std::get_if<OrderRestingNotification>(sp)) {
        // track resting order id
        if (agent.open_order_count < 16) {
            agent.open_order_ids[agent.open_order_count++] = resting->order_id;
        }
    }
}

// ============================================================
// Retail react:  sleep-cycle filtered, dispatches to strategy
// ============================================================
inline void retail_react(RetailAgent& agent, uint64_t itch_ts,
                         const BrokerSnapshot& snap, const MarketState& ms,
                         std::deque<Event>& rq)
{
    if (!agent.is_active) return;
    if (snap.best_bid == 0 || snap.best_ask == 0) return;

    // ADDED: sleep cycle check
    uint64_t agent_receive = itch_ts + agent.l1;
    if (agent_receive - agent.last_react_ns < Config::RETAIL_SLEEP_NS) return;

    agent.last_react_ns = agent_receive;

    switch (agent.strategy) {
        case Config::RETAIL_STRAT_TREND_LIMIT:
            strategy_retail_trend_limit(agent, itch_ts, snap, rq);
            break;
        case Config::RETAIL_STRAT_TREND_MARKET:
            strategy_retail_trend_market(agent, itch_ts, snap, rq);
            break;
        case Config::RETAIL_STRAT_MEAN_REVERT:
            strategy_retail_mean_revert(agent, itch_ts, snap, rq);
            break;
        case Config::RETAIL_STRAT_NOISE:
            strategy_retail_noise(agent, itch_ts, snap, rq);
            break;
        case Config::RETAIL_STRAT_SCARE:
            strategy_retail_scare(agent, itch_ts, snap, ms, rq);
            break;
        default:
            strategy_retail_noise(agent, itch_ts, snap, rq);
            break;
    }
}

// ============================================================
// Fundamentalist react : called only on FairPriceUpdate event
// ============================================================
/*(in development , need to incorporate errors . for Estimated fair price ----
   ---- Hestom Model for fair price . agents estimate with errors.)*/  
inline void fund_react(FundamentalistAgent& agent, uint64_t fair_price,
                       const MarketState& ms, std::deque<Event>& rq,
                       uint64_t event_seq)
{
    if (!agent.is_active) return;
    if (ms.best_bid == 0 || ms.best_ask == 0) return;

    uint64_t mid = (ms.best_bid + ms.best_ask) / 2;
    int64_t  dev = (int64_t)fair_price - (int64_t)mid;
    uint64_t abs_dev = (uint64_t)std::abs(dev);
    if (abs_dev < agent.correction_threshold) return;

    bool buy = (dev > 0);
    // CHANGED: use limit order at fair price, not market order
    // less disruptive, still corrects price
    uint64_t limit_price = buy
        ? ms.best_ask + Config::TICK_SIZE  // slightly aggressive
        : ms.best_bid - Config::TICK_SIZE;

    Event e;
    e.event_type       = EventType::OUCH;
    e.symbol           = Symbol::AAPL;
    e.agent_tier       = AgentTier::Fundamentalist;
    e.agent_index      = agent.id;
    e.causal_parent_id = event_seq;
    e.payload = OUCHPayload{ EnterLimitOrder{
        available_order_id++, limit_price, agent.order_size,
        buy ? Order_Side::Buy : Order_Side::Sell, 0
    }};
    rq.push_back(e);
}

// ============================================================
// Institutional react -  called on ITCH events, time-driven
// ============================================================
inline void inst_react(InstitutionalAgent& agent, uint64_t event_ts,
                       const MarketState& ms, std::deque<Event>& rq)
{
    if (!agent.is_active) return;
    if (agent.remaining_quantity <= 0) return;

    switch (agent.strategy) {
        case Config::INST_STRAT_BLOCK_BUY:
        case Config::INST_STRAT_BLOCK_SELL:
            strategy_inst_block(agent, event_ts, ms, rq);
            break;
        case Config::INST_STRAT_TWAP:
            strategy_inst_twap(agent, event_ts, ms, rq);
            break;
        default:
            break;
    }
}
