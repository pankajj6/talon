#pragma once
// ============================================================
// engine.h
// Matching engine. Called by kernel in MODE 1.
//
// MODE REFERENCE:
//   MODE 1 = kernel pops OUCH  -> calls engine (this file) 
//   MODE 2 = kernel pops ITCH  -> iterates all agents
//   MODE 3 = kernel pops SPECIFIC_OUCH -> updates one agent
//
// Push ordering rule (STRICT):
//   For any trade: passive_specific -> aggr_specific → public ITCH
//   For cancel success: CancelAccepted -> public OrderCancelled ITCH
//   For replace success: ReplaceAccepted -> public OrderReplaced ITCH
//
// CHANGED from previous version:
//   - process_update() fully implemented (was TODO stub)
//   - remove_price_level() calls now pass side parameter
//     (lob.h changed remove_price_level to take side — was erasing
//     both bid and ask regardless of which side the order was on)
//   - ReplaceAccepted and ReplaceRejected used in process_update
//   - push_order_replaced_itch() helper added for public broadcast
//     after a successful replace
// ============================================================

#include <deque>
#include <cstdint>
#include "events.h"
#include "order.h"
#include "oldlob.h"
#include "config.h"
#include "status.h"

// ============================================================
// HELPER: build Order from EnterLimitOrder
// ============================================================




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
    e.agent_tier       = passive_tier;
    e.agent_index      = passive_index;
    e.payload = ITCHPayload{ OrderExecuted{
                    passive_order_id, price, filled_qty, e.sequence_num }};
    feed_hq.push_back(e);
}

// ============================================================
// HELPER: push public OrderAdded ITCH
// ============================================================
inline void push_order_added(
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
    int32_t            cancelled_qty,
    Order_Side         side,
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
    e.payload = ITCHPayload{ OrderCancelled{ order_id, cancelled_qty, side}};
    feed_hq.push_back(e);
}

// ============================================================
// HELPER: push public OrderReplaced ITCH
// CHANGED: new helper for process_update success path.
// ============================================================
inline void push_order_replaced_itch(
    std::deque<Event>& feed_hq,
    uint64_t           old_order_id,
    uint64_t           new_order_id,
    uint64_t           new_price,
    int32_t            new_quantity,
    int32_t            old_quantity, // for volume , we need it.
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
                    old_order_id, new_order_id,
                    new_price, new_quantity , old_quantity, side }};
    feed_hq.push_back(e);
}


// ============================================================
// ENGINE CLASS
// ============================================================
class Engine
{
public:

    // void process_ouch_request(Event&             event,
    //                           LOB&               lob,
    //                           std::deque<Event>& feed_hq,
    //                           uint64_t&          seq_num)
    // {
    //     if (event.timestamp > lob.clock)
    //         lob.clock = event.timestamp;

    //     auto* ouch_ptr = std::get_if<OUCHPayload>(&event.payload);
    //     if (!ouch_ptr) return;

    //     if (auto* o = std::get_if<EnterLimitOrder>(ouch_ptr)) { // duplicate order_id . reject.
    //         if (lob.orders_by_Id.count(o->order_id)) {
    //             lob.clock += Config::PT_BASE;
    //             push_specific_ouch(feed_hq,
    //                 event.agent_tier, event.agent_index, event.symbol,
    //                 SpecificOUCHPayload{ OrderRejected{ o->order_id, Reason::duplicate_order_id}},
    //                 lob.clock, seq_num, event.sequence_num);
    //             return;
    //         }
    //         process_limit_order(event, *o, lob, feed_hq, seq_num);
    //     }

    //     else if (auto* o = std::get_if<EnterMarketOrder>(ouch_ptr)) { // same
    //         if (lob.orders_by_Id.count(o->order_id)) {
    //             lob.clock += Config::PT_BASE;
    //             push_specific_ouch(feed_hq,
    //                 event.agent_tier, event.agent_index, event.symbol,
    //                 SpecificOUCHPayload{ OrderRejected{ o->order_id, Reason::duplicate_order_id }},lob.clock,
    //                 seq_num, event.sequence_num);
    //             return;
    //         }
    //         process_market_order(event, *o, lob, feed_hq, seq_num);
    //     }

    //     else if (auto* o = std::get_if<CancelOrder>(ouch_ptr)) {
    //         process_cancel(event, *o, lob, feed_hq, seq_num);
    //     }

    //     else if (auto* o = std::get_if<ReplaceOrder>(ouch_ptr)) {
    //         process_update(event, *o, lob, feed_hq, seq_num);
    //     }
    // }

private:

    void process_limit_order(Event&             event,
                             EnterLimitOrder&   incoming,
                             LOB&               lob,
                             std::deque<Event>& feed_hq,
                             uint64_t&          seq_num)
    {
        lob.clock += Config::PT_BASE;

        if (incoming.price == 0) {
            push_specific_ouch(feed_hq,
                event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ OrderRejected{ incoming.order_id, Reason::invalid_price }},
                lob.clock, seq_num, event.sequence_num);
            return;
        }

        if(incoming.quantity <= 0){
            push_specific_ouch(feed_hq,
                event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ OrderRejected{ incoming.order_id, Reason::invalid_quantity}},
                lob.clock, seq_num, event.sequence_num);
            return;
        }

        if(incoming.price); // put a check if tick size voilates... 

        Order l_order = build_order(incoming, incoming.order_id);
        l_order.agent_tier  = event.agent_tier;
        l_order.agent_index = event.agent_index;

        if (incoming.side == Order_Side::Buy) // BUY
        {
            while (l_order.quantity > 0
                   && lob.best_ask() != 0
                   && lob.best_ask() <= l_order.price)
            {
                uint64_t     best_ask = lob.best_ask();
                price_level& lvl   = lob.ask.at(best_ask);
                Order& front = lob.order_pool.storage_pool[lvl.head];

                if (l_order.quantity < front.quantity) {
                    front.quantity -= l_order.quantity;
                    lob.clock += Config::PT_ORDER_FILL;
                    push_fill_pair(feed_hq,
                        front.order_id,
                        front.agent_tier, front.agent_index,
                        front.quantity, front.side,
                        incoming.order_id,
                        event.agent_tier, event.agent_index,
                        0, incoming.side,
                        l_order.quantity, best_ask, event.symbol,
                        lob.clock, seq_num, event.sequence_num);
                    l_order.quantity = 0;
                    return;
                }
                else {
                    int32_t fill_qty = front.quantity;
                    int32_t aggr_rem = l_order.quantity - fill_qty;
                    lob.clock += Config::PT_ORDER_FILL;
                    push_fill_pair(feed_hq,
                        front.order_id,
                        front.agent_tier, front.agent_index,
                        0, front.side,
                        incoming.order_id,
                        event.agent_tier, event.agent_index,
                        aggr_rem, incoming.side,
                        fill_qty, best_ask, event.symbol,
                        lob.clock, seq_num, event.sequence_num);
                    l_order.quantity -= fill_qty;
                    Status s = lob.order_pool.move_next_order(lvl);
                    if (s == Status::FAILURE) {
                        // CHANGED: pass side to remove correct map side
                        lob.remove_price_level(best_ask, Order_Side::Sell); // ask side
                        if (lob.best_ask() != 0 && lob.best_ask() <= l_order.price)
                            lob.clock += Config::PT_LEVEL_WALK;
                    }
                }
            }
            if (l_order.quantity > 0) {
                lob.clock += Config::PT_ADD_ORDER;
                lob.order_pool.add_order(lob, l_order);
                push_specific_ouch(feed_hq,
                    event.agent_tier, event.agent_index, event.symbol,
                    SpecificOUCHPayload{ OrderRestingNotification{
                        l_order.order_id, l_order.price,
                        l_order.quantity, l_order.side }},
                    lob.clock, seq_num, event.sequence_num);
                push_order_added(feed_hq, l_order, event.symbol,
                                 lob.clock, seq_num, event.sequence_num);
            }
        }
        else // SELL
        {
            while (l_order.quantity > 0
                   && lob.best_bid() != 0
                   && lob.best_bid() >= l_order.price)
            {
                uint64_t     best_bid = lob.best_bid();
                price_level& lvl      = lob.bid.at(best_bid);
                Order& front = lob.order_pool.storage_pool[lvl.head];

                if (l_order.quantity < front.quantity) {
                    front.quantity -= l_order.quantity;
                    lob.clock += Config::PT_ORDER_FILL;
                    push_fill_pair(feed_hq,
                        front.order_id,
                        front.agent_tier, front.agent_index,
                        front.quantity, front.side,
                        incoming.order_id,
                        event.agent_tier, event.agent_index,
                        0, incoming.side,
                        l_order.quantity, best_bid, event.symbol,
                        lob.clock, seq_num, event.sequence_num);
                    l_order.quantity = 0;
                    return;
                }
                else {
                    int32_t fill_qty = front.quantity;
                    int32_t aggr_rem = l_order.quantity - fill_qty;
                    lob.clock += Config::PT_ORDER_FILL;
                    push_fill_pair(feed_hq,
                        front.order_id,
                        front.agent_tier, front.agent_index,
                        0, front.side,
                        incoming.order_id,
                        event.agent_tier, event.agent_index,
                        aggr_rem, incoming.side,
                        fill_qty, best_bid, event.symbol,
                        lob.clock, seq_num, event.sequence_num);
                    l_order.quantity -= fill_qty;
                    Status s = lob.order_pool.move_next_order(lvl);
                    if (s == Status::FAILURE) {
                        // CHANGED: pass side
                        lob.remove_price_level(best_bid, Order_Side::Buy); // bid side
                        if (lob.best_bid() != 0 && lob.best_bid() >= l_order.price)
                            lob.clock += Config::PT_LEVEL_WALK;
                    }
                }
            }
            if (l_order.quantity > 0) {
                lob.clock += Config::PT_ADD_ORDER;
                lob.order_pool.add_order(lob, l_order);
                push_specific_ouch(feed_hq,
                    event.agent_tier, event.agent_index, event.symbol,
                    SpecificOUCHPayload{ OrderRestingNotification{
                        l_order.order_id, l_order.price,
                        l_order.quantity, l_order.side }},
                    lob.clock, seq_num, event.sequence_num);
                push_order_added(feed_hq, l_order, event.symbol,
                                 lob.clock, seq_num, event.sequence_num);
            }
        }
    }


    void process_market_order(Event&             event,
                              EnterMarketOrder&  m_order,
                              LOB&               lob,
                              std::deque<Event>& feed_hq,
                              uint64_t&          seq_num)
    {
        lob.clock += Config::PT_BASE;
        if (m_order.quantity <= 0) {
            push_specific_ouch(feed_hq,
                event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ OrderRejected{ m_order.order_id, Reason::invalid_quantity}},
                lob.clock, seq_num, event.sequence_num);
            return;
        }

        if (m_order.side == Order_Side::Buy)
        {
            while (m_order.quantity > 0) {
                uint64_t best_ask = lob.best_ask();
                if (best_ask == 0) break; // book empty
                price_level& lvl   = lob.ask.at(best_ask);
                Order& front = lob.order_pool.storage_pool[lvl.head];
                if (m_order.quantity < front.quantity) {
                    front.quantity -= m_order.quantity;
                    lob.clock += Config::PT_ORDER_FILL;
                    push_fill_pair(feed_hq,
                        front.order_id,
                        front.agent_tier, front.agent_index,
                        front.quantity, front.side,
                        m_order.order_id,
                        event.agent_tier, event.agent_index,
                        0, m_order.side,
                        m_order.quantity, best_ask, event.symbol,
                        lob.clock, seq_num, event.sequence_num);
                    m_order.quantity = 0;
                    break;
                } else {
                    int32_t fill_qty = front.quantity;
                    int32_t rem      = m_order.quantity - fill_qty;
                    lob.clock += Config::PT_ORDER_FILL;
                    push_fill_pair(feed_hq,
                        front.order_id,
                        front.agent_tier, front.agent_index,
                        0, front.side,
                        m_order.order_id,
                        event.agent_tier, event.agent_index,
                        rem, m_order.side,
                        fill_qty, best_ask, event.symbol,
                        lob.clock, seq_num, event.sequence_num);
                    m_order.quantity -= fill_qty;
                    Status s = lob.order_pool.move_next_order(lvl);
                    if (s == Status::FAILURE) {
                        lob.remove_price_level(best_ask, Order_Side::Sell); // CHANGED: side
                        lob.clock += Config::PT_LEVEL_WALK;
                    }
                }
            }
            if (m_order.quantity > 0) {
                push_specific_ouch(feed_hq,
                    event.agent_tier, event.agent_index, event.symbol,
                    SpecificOUCHPayload{ OrderRejected{ m_order.order_id, Reason::book_empty}},
                    lob.clock, seq_num, event.sequence_num);
            }
        }
        else // SELL
        {
            while (m_order.quantity > 0) {
                uint64_t best_bid = lob.best_bid();
                if (best_bid == 0) break;
                price_level& lvl   = lob.bid.at(best_bid);
                Order& front = lob.order_pool.storage_pool[lvl.head];
                if (m_order.quantity < front.quantity) {
                    front.quantity -= m_order.quantity;
                    lob.clock += Config::PT_ORDER_FILL;
                    push_fill_pair(feed_hq,
                        front.order_id,
                        front.agent_tier, front.agent_index,
                        front.quantity, front.side,
                        m_order.order_id,
                        event.agent_tier, event.agent_index,
                        0, m_order.side,
                        m_order.quantity, best_bid, event.symbol,
                        lob.clock, seq_num, event.sequence_num);
                    m_order.quantity = 0;
                    break;
                } else {
                    int32_t fill_qty = front.quantity;
                    int32_t rem      = m_order.quantity - fill_qty;
                    lob.clock += Config::PT_ORDER_FILL;
                    push_fill_pair(feed_hq,
                        front.order_id,
                        front.agent_tier, front.agent_index,  // FIXED: was using wrong order of args previously
                        0, front.side,
                        m_order.order_id,
                        event.agent_tier, event.agent_index,
                        rem, m_order.side,
                        fill_qty, best_bid, event.symbol,
                        lob.clock, seq_num, event.sequence_num);
                    m_order.quantity -= fill_qty;
                    Status s = lob.order_pool.move_next_order(lvl);
                    if (s == Status::FAILURE) {
                        lob.remove_price_level(best_bid, Order_Side::Buy); // CHANGED: side
                        lob.clock += Config::PT_LEVEL_WALK;
                    }
                }
            }
            if (m_order.quantity > 0) {
                push_specific_ouch(feed_hq,
                    event.agent_tier, event.agent_index, event.symbol,
                    SpecificOUCHPayload{ OrderRejected{ m_order.order_id, Reason::book_empty }},
                    lob.clock, seq_num, event.sequence_num);
            }
        }
    }


    void process_cancel(
        Event& event, CancelOrder& cancel, LOB& lob,
        std::deque<Event>& feed_hq, uint64_t& seq_num)
    {
        lob.clock += Config::PT_BASE;

        auto it = lob.orders_by_Id.find(cancel.order_id);
        if (it == lob.orders_by_Id.end()) {
            push_specific_ouch(feed_hq, event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ CancelRejected{ cancel.order_id, Reason::orderId_NOT_Found}},
                lob.clock, seq_num, event.sequence_num);
            return;
        }

        uint32_t order_index = it->second;
        Order& order = lob.order_pool.storage_pool[order_index];

        price_level* p = nullptr;
        if (order.side == Order_Side::Sell) {
            auto it2 = lob.ask.find(order.price);
            if (it2 != lob.ask.end()) p = &it2->second;
        } else {
            auto it2 = lob.bid.find(order.price);
            if (it2 != lob.bid.end()) p = &it2->second;
        }
        if (!p) return;

        int32_t max_qty = cancel.max_quantity;

        if (max_qty > 0 && max_qty < order.quantity) {
            int32_t cancelled = order.quantity - max_qty;
            order.quantity    = max_qty;
            lob.clock        += Config::PT_CANCEL;
            push_specific_ouch(feed_hq,
                event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ CancelAccepted{ cancel.order_id, max_qty }},
                lob.clock, seq_num, event.sequence_num);
            push_order_cancelled_itch(feed_hq,
                cancel.order_id, cancelled, order.side,
                event.agent_tier, event.agent_index, event.symbol,
                lob.clock, seq_num, event.sequence_num);
        }
        else if (max_qty == 0) {
            int32_t cancelled = order.quantity;
            lob.clock        += Config::PT_CANCEL;
            lob.order_pool.delete_order(lob, *p, order_index);
            push_specific_ouch(feed_hq,
                event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ CancelAccepted{ cancel.order_id, 0 }},
                lob.clock, seq_num, event.sequence_num);
            push_order_cancelled_itch(feed_hq,
                cancel.order_id, cancelled, order.side, 
                event.agent_tier, event.agent_index, event.symbol,
                lob.clock, seq_num, event.sequence_num);
        }
        // else: max_qty >= current qty or negative — ignore
    }


    // --------------------------------------------------------
    // process_update — CHANGED: fully implemented (was TODO stub)
    //
    // Replace = cancel old order + insert new order at new price.
    // Always loses queue priority (new order goes to tail).
    //
    // Your logic from notebook kept:
    //   1. Find old order by old_order_id
    //   2. Save its qty (that's old_cancelled_qty)
    //   3. Build new Order from replace fields
    //   4. delete old, add new, increment clocks
    //   5. Push ReplaceAccepted Specific_OUCH
    //   6. Push public OrderReplaced ITCH after
    //
    // CHANGED from your sketch:
    //   - delete_order takes (lob, price_level&, index) not (lob, order)
    //     so we need to find the price_level first (same as cancel)
    //   - new_order.new_quantity -> replace.new_quantity (field name fix)
    //   - ReplaceRejected uses old_order_id (more sensible than new_order_id
    //     when rejection is because old order doesn't exist)
    // --------------------------------------------------------
    void process_update(Event&             event,
                        ReplaceOrder&      replace,
                        LOB&               lob,
                        std::deque<Event>& feed_hq,
                        uint64_t&          seq_num)
    {
        lob.clock += Config::PT_BASE;

        auto it = lob.orders_by_Id.find(replace.old_order_id);
        if (it == lob.orders_by_Id.end()) {
            // CHANGED: ReplaceRejected with reason 0
            push_specific_ouch(feed_hq,
                event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ ReplaceRejected{
                    replace.old_order_id, 0 }},
                lob.clock, seq_num, event.sequence_num);
            return;
        }

        uint32_t order_index  = it->second;
        Order&  old_order  = lob.order_pool.storage_pool[order_index];
        int32_t  old_cancelled = old_order.quantity;
        Order_Side  side = old_order.side;

        // Find price level of old order (same side logic as cancel)
        price_level* p = nullptr;
        if (side == Order_Side::Sell) {
            auto it2 = lob.ask.find(old_order.price);
            if (it2 != lob.ask.end()) p = &it2->second;
        } else {
            auto it2 = lob.bid.find(old_order.price);
            if (it2 != lob.bid.end()) p = &it2->second;
        }
        if (!p) return;

        // Build replacement order
        Order new_order;
        new_order.order_id    = replace.new_order_id;
        new_order.price       = replace.new_price;
        new_order.quantity    = replace.new_quantity; // CHANGED: was new_order.new_quantity
        new_order.side        = side;
        new_order.agent_tier  = old_order.agent_tier;
        new_order.agent_index = old_order.agent_index;
        new_order.next        = -1;
        new_order.prev        = -1;

        // Delete old, add new — new order goes to tail (loses priority)
        lob.order_pool.delete_order(lob, *p, order_index);
        lob.clock += Config::PT_CANCEL;     // cost of removing old

        lob.order_pool.add_order(lob, new_order);
        lob.clock += Config::PT_ADD_ORDER;  // cost of inserting new

        // Private confirmation to agent first
        push_specific_ouch(feed_hq,
            event.agent_tier, event.agent_index, event.symbol,
            SpecificOUCHPayload{ ReplaceAccepted{
                new_order.order_id,
                new_order.price,
                old_cancelled,
                new_order.quantity,
                side
            }},
            lob.clock, seq_num, event.sequence_num);

        // Public broadcast after — replace is public (book state changed)
        push_order_replaced_itch(feed_hq,
            replace.old_order_id, new_order.order_id,
            new_order.price, new_order.quantity, old_cancelled,
            new_order.side, event.agent_tier, event.agent_index,
            event.symbol,lob.clock, seq_num, event.sequence_num);
    }

}; // class Engine
