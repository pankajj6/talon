#pragma once
#include <deque>
#include <cstdint>
#include "events.h"
#include "order.h"
#include "lob.h"
#include "config.h"
#include "status.h"



inline Order build_order(const EnterLimitOrder& incoming, uint64_t order_id)
{
    Order o;
    o.price       = incoming.price;
    o.quantity    = incoming.quantity;
    o.order_id    = order_id;
    o.side        = incoming.side;
    o.next        = -1;
    o.prev        = -1;
    return o;
}

// ============================================================
// HELPER: push SPECIFIC_OUCH for one agent
// ============================================================

inline void push_specific_ouch(
    
    std::deque<Event>&   feed_hq, AgentTier  target_tier, uint32_t  target_index, Symbol  symbol,
    SpecificOUCHPayload  payload,
    uint64_t   lob_clock, uint64_t&  seq_num,
    uint64_t causal_parent_id
)
{
    Event e;
    e.timestamp        = lob_clock;
    e.sequence_num     = seq_num++;
    e.causal_parent_id = causal_parent_id;
    e.event_type       = EventType::SPECIFIC_OUCH;
    e.symbol           = symbol;
    e.agent_tier       = target_tier;
    e.agent_index      = target_index;
    e.payload          = payload;
    feed_hq.push_back(e);
}

// ============================================================
// HELPER: push fill pair — both specific + public ITCH
// Strict order: passive_specific -> aggr_specific -> ITCH
// ============================================================
inline void push_fill_pair(
    std::deque<Event>&  feed_hq,
    uint64_t            passive_order_id,
    AgentTier           passive_tier,
    uint32_t            passive_index,
    int32_t             passive_remaining,
    Order_Side          passive_side,
    uint64_t            aggr_order_id,
    AgentTier           aggr_tier,
    uint32_t            aggr_index,
    int32_t             aggr_remaining,
    Order_Side          aggr_side,
    int32_t             filled_qty,
    uint64_t            price,
    Symbol              symbol,
    uint64_t            lob_clock,
    uint64_t&           seq_num,
    uint64_t            causal_parent_id)
{
    push_specific_ouch(feed_hq, passive_tier, passive_index, symbol,
        SpecificOUCHPayload{ FillNotification{
            passive_order_id, filled_qty, price,
            passive_side, passive_remaining }},
        lob_clock, seq_num, causal_parent_id);

    push_specific_ouch(feed_hq, aggr_tier, aggr_index, symbol,
        SpecificOUCHPayload{ FillNotification{
            aggr_order_id, filled_qty, price,
            aggr_side, aggr_remaining }},
        lob_clock, seq_num, causal_parent_id);

    Event e;
    e.timestamp        = lob_clock;
    e.sequence_num     = seq_num++;
    e.causal_parent_id = causal_parent_id;
    e.event_type       = EventType::ITCH;
    e.symbol           = symbol;
    e.agent_tier       = passive_tier; // 
    e.agent_index      = passive_index;
    e.payload = ITCHPayload{ OrderExecuted{
                    passive_order_id, price, filled_qty, passive_side, e.sequence_num }};
    feed_hq.push_back(e);
}

// ============================================================
// HELPER: push public OrderAdded ITCH
// ============================================================
inline void push_order_added_itch(
    std::deque<Event>& feed_hq,
    const Order&       order,
    Symbol             symbol,
    uint64_t           lob_clock,
    uint64_t&          seq_num,
    uint64_t           causal_parent_id)
{
    Event e;
    e.timestamp        = lob_clock;
    e.sequence_num     = seq_num++;
    e.causal_parent_id = causal_parent_id;
    e.event_type       = EventType::ITCH;
    e.symbol           = symbol;
    e.agent_tier       = order.agent_tier;
    e.agent_index      = order.agent_index;
    e.payload = ITCHPayload{ OrderAdded{
                    order.order_id, order.price,
                    order.quantity, order.side }};
    feed_hq.push_back(e);
}

// ============================================================
// HELPER: push public OrderCancelled ITCH
// ============================================================
inline void push_order_cancelled_itch(
    std::deque<Event>& feed_hq,
    uint64_t           order_id,
    uint64_t            price,
    int32_t            cancelled_qty,
    Order_Side          side,
    AgentTier          tier,
    uint32_t           agent_index,
    Symbol             symbol,
    uint64_t           lob_clock,
    uint64_t&          seq_num,
    uint64_t           causal_parent_id)
{
    Event e;
    e.timestamp        = lob_clock;
    e.sequence_num     = seq_num++;
    e.causal_parent_id = causal_parent_id;
    e.event_type       = EventType::ITCH;
    e.symbol           = symbol;
    e.agent_tier       = tier;
    e.agent_index      = agent_index;
    e.payload = ITCHPayload{ OrderCancelled{ order_id, price ,cancelled_qty, side}};
    feed_hq.push_back(e);
}

// ============================================================
// HELPER: push public OrderReplaced ITCH
// CHANGED: new helper for process_update success path.
// ============================================================
inline void push_order_replaced_itch(
    std::deque<Event>& feed_hq,
    uint64_t           old_order_id,
    uint64_t           old_price ,
    int32_t            old_cancelled_qty, // for volume , we need it.
    uint64_t           new_order_id,
    uint64_t           new_price,
    int32_t            new_added_qty,
    Order_Side         side, // need it in market state.
    AgentTier          tier,
    uint32_t           agent_index,
    Symbol             symbol,
    uint64_t           lob_clock,
    uint64_t&          seq_num,
    uint64_t           causal_parent_id)
{
    Event e;
    e.timestamp        = lob_clock;
    e.sequence_num     = seq_num++;
    e.causal_parent_id = causal_parent_id;
    e.event_type       = EventType::ITCH;
    e.symbol           = symbol;
    e.agent_tier       = tier;
    e.agent_index      = agent_index;
    e.payload = ITCHPayload{ OrderReplaced{
                    old_order_id, old_price , old_cancelled_qty ,new_order_id,
                    new_price, new_added_qty , side }};
    feed_hq.push_back(e);
}


class Engine {
    public:
    void process_ouch_request(Event& event,LOB& lob, std::deque<Event>& feed_hq, uint64_t& seq_num);
    // public:
    void process_limit_order(Event& event, EnterLimitOrder& l_order, LOB& lob, std::deque<Event>& feed_hq, uint64_t& seq_num);
    void process_market_order(Event& event, EnterMarketOrder& m_order, LOB& lob, std::deque<Event>& feed_hq, uint64_t& seq_num);
    void process_cancel(Event& event, CancelOrder& cancel, LOB& lob, std::deque<Event>& feed_hq, uint64_t& seq_num);
    void process_update(Event& event, ReplaceOrder& r_order, LOB& lob, std::deque<Event>& feed_hq, uint64_t& seq_num);
};  
