#include <deque>
#include <cstdint>
#include "events.h"
#include "order.h"
#include "config.h"
#include "status.h"
#include "engine.h"
#include <iostream>

void Engine::process_ouch_request(Event&  event,
                              LOB&   lob,
                              std::deque<Event>& feed_hq,
                              uint64_t&  seq_num)
{
    if (event.timestamp > lob.clock)
        {lob.clock = event.timestamp;}

    auto* ouch_ptr = std::get_if<OUCHPayload>(&event.payload);
    if (!ouch_ptr) return;

    if (auto* o = std::get_if<EnterLimitOrder>(ouch_ptr)) { // duplicate order_id . reject.
        if (lob.orders_by_Id.count(o->order_id)) {
            lob.clock += Config::PT_BASE;
            push_specific_ouch(feed_hq,
                event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ OrderRejected{ o->order_id, Reason::duplicate_order_id}},
                lob.clock, seq_num, event.sequence_num);
            return;
        }
        process_limit_order(event, *o, lob, feed_hq, seq_num);
    }

    else if (auto* o = std::get_if<EnterMarketOrder>(ouch_ptr)) { // same
        if (lob.orders_by_Id.count(o->order_id)) {
            lob.clock += Config::PT_BASE;
            push_specific_ouch(feed_hq,
                event.agent_tier, event.agent_index, event.symbol,
                SpecificOUCHPayload{ OrderRejected{ o->order_id, Reason::duplicate_order_id }},lob.clock,
                seq_num, event.sequence_num);
            return;
        }
        process_market_order(event, *o, lob, feed_hq, seq_num);
    }

    else if (auto* o = std::get_if<CancelOrder>(ouch_ptr)) {
        process_cancel(event, *o, lob, feed_hq, seq_num);
    }

    else if (auto* o = std::get_if<ReplaceOrder>(ouch_ptr)) {
        process_update(event, *o, lob, feed_hq, seq_num);
    }
}



void Engine::process_limit_order(Event&  event,
                            EnterLimitOrder&   incoming,
                            LOB&    lob,
                            std::deque<Event>& feed_hq,
                            uint64_t&   seq_num)
{
    lob.clock += Config::PT_BASE;

    if (incoming.price == 0 || incoming.price%Config::TICK_SIZE != 0) { // tick size check added.
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

    // if (incoming.side == Order_Side::Buy) // BUY
    // {
    //     std::cout << "[TRACER] Processing BUY Side. Order Qty: " << l_order.quantity << std::endl;
        
    //     while (l_order.quantity > 0 && lob.best_ask() != 0 && lob.best_ask() <= l_order.price)
    //     {
    //         std::cout << "[TRACER] Entering fill loop..." << std::endl;
    //         uint64_t best_ask = lob.best_ask();
    //         auto* lvl = lob.ask.get_level(best_ask);
            
    //         if (lvl == nullptr) {
    //             std::cout << "[FATAL] best_ask returned " << best_ask << " but get_level is NULL!" << std::endl;
    //             exit(1);
    //         }
            
    //         std::cout << "[TRACER] Dereferencing front order..." << std::endl;
    //         Order& front = lob.storage_pool[lvl->head];

    //         if (l_order.quantity < front.quantity) {
    //             std::cout << "[TRACER] Partial fill of resting order..." << std::endl;
    //             front.quantity -= l_order.quantity;
    //             lob.clock += Config::PT_ORDER_FILL;
    //             // ... your push_fill_pair logic ...
    //             l_order.quantity = 0;
    //             return;
    //         }
    //         else {
    //             std::cout << "[TRACER] Full fill of resting order..." << std::endl;
    //             // ... your push_fill_pair logic ...
    //             Status s = lob.move_next_order(*lvl);
    //             if (s == Status::FAILURE) {
    //                 std::cout << "[TRACER] Erasing price level..." << std::endl;
    //                 lob.ask.erase_level(best_ask); 
    //                 if (lob.best_ask() != 0 && lob.best_ask() <= l_order.price)
    //                     lob.clock += Config::PT_LEVEL_WALK;
    //             }
    //         }
    //     }
    //     if (l_order.quantity > 0) {
    //         std::cout << "[TRACER] Adding order to LOB..." << std::endl;
    //         lob.clock += Config::PT_ADD_ORDER;
    //         Status status = lob.add_order(l_order);
            
    //         std::cout << "[TRACER] Order added. Status = " << (int)status << std::endl;
            
    //         if (status == Status::SUCCESS) {
    //             // ... your push_specific_ouch and push_order_added_itch logic ...
    //         }
    //         else {
    //             std::cout << "[TRACER] LOB Full! Rejecting order..." << std::endl;
    //             // ... your push_specific_ouch reject logic ...
    //         }
    //     }    
    // }
    if (incoming.side == Order_Side::Buy) // BUY
    {

        while (l_order.quantity > 0 && lob.best_ask() != 0 && lob.best_ask() <= l_order.price)
        {
            uint64_t     best_ask = lob.best_ask();
            auto* lvl   = lob.ask.get_level(best_ask);
            Order& front = lob.storage_pool[lvl->head];

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
                Status s = lob.move_next_order(*lvl);
                if (s == Status::FAILURE) {
                    // CHANGED: pass side to remove correct map side
                    lob.ask.erase_level(best_ask); // remove_price_level(best_ask, Order_Side::Sell); // ask side
                    if (lob.best_ask() != 0 && lob.best_ask() <= l_order.price)
                        lob.clock += Config::PT_LEVEL_WALK;
                }
            }
        }
        if (l_order.quantity > 0) {
            lob.clock += Config::PT_ADD_ORDER;
            Status status = lob.add_order(l_order);
            if (status == Status::SUCCESS) {
                push_specific_ouch(feed_hq,
                    event.agent_tier, event.agent_index, event.symbol,
                    SpecificOUCHPayload{ OrderRestingNotification{
                        l_order.order_id, l_order.price,
                        l_order.quantity, l_order.side }},
                lob.clock, seq_num, event.sequence_num);
            push_order_added_itch(feed_hq, l_order, event.symbol,
                                lob.clock, seq_num, event.sequence_num);
            }
            else { // look here the logic:
                push_specific_ouch(feed_hq,
                    event.agent_tier, event.agent_index, event.symbol,
                    SpecificOUCHPayload{ OrderRejected{ l_order.order_id, Reason::lob_full }},
                lob.clock, seq_num, event.sequence_num);
            }
        }    
    }
    else // SELL
    {
        while (l_order.quantity > 0
                && lob.best_bid() != 0
                && lob.best_bid() >= l_order.price)
        {
            uint64_t     best_bid = lob.best_bid();
            auto* lvl = lob.bid.get_level(best_bid);
            Order& front = lob.storage_pool[lvl->head];

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
                Status s = lob.move_next_order(*lvl);
                if (s == Status::FAILURE) {
                    lob.bid.erase_level(best_bid); // bid side
                    if (lob.best_bid() != 0 && lob.best_bid() >= l_order.price)
                        lob.clock += Config::PT_LEVEL_WALK;
                }
            }
        }
        if (l_order.quantity > 0) {
            lob.clock += Config::PT_ADD_ORDER;
            Status status= lob.add_order(l_order);
            if (status == Status::SUCCESS) {
                push_specific_ouch(feed_hq,
                    event.agent_tier, event.agent_index, event.symbol,
                    SpecificOUCHPayload{ OrderRestingNotification{
                        l_order.order_id, l_order.price,
                        l_order.quantity, l_order.side }},
                lob.clock, seq_num, event.sequence_num);
            push_order_added_itch(feed_hq, l_order, event.symbol,
                                lob.clock, seq_num, event.sequence_num);
            }
            else { // look here the logic:
                    push_specific_ouch(feed_hq,
                        event.agent_tier, event.agent_index, event.symbol,
                        SpecificOUCHPayload{ OrderRejected{ l_order.order_id, Reason::lob_full }},
                    lob.clock, seq_num, event.sequence_num);
            }
        }
    }
}



void Engine::process_market_order(Event&             event,
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
            auto* lvl   = lob.ask.get_level(best_ask);
            Order& front = lob.storage_pool[lvl->head];
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
                Status s = lob.move_next_order(*lvl);
                if (s == Status::FAILURE) {
                    lob.ask.erase_level(best_ask);
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
            auto* lvl   = lob.bid.get_level(best_bid);
            Order& front = lob.storage_pool[lvl->head];
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
                    front.agent_tier, front.agent_index,
                    0, front.side,
                    m_order.order_id,
                    event.agent_tier, event.agent_index,
                    rem, m_order.side,
                    fill_qty, best_bid, event.symbol,
                    lob.clock, seq_num, event.sequence_num);
                m_order.quantity -= fill_qty;
                Status s = lob.move_next_order(*lvl);
                if (s == Status::FAILURE) {
                    lob.bid.erase_level(best_bid);
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



void Engine::process_cancel( // here adding logic of updating price level liquidity.
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
    Order& order = lob.storage_pool[order_index];

    price_level* p = nullptr;
    if (order.side == Order_Side::Sell) p = lob.ask.get_level(order.price);
    else p = lob.bid.get_level(order.price);
    if (!p) return;

    int32_t max_qty = cancel.max_quantity;

    if (max_qty > 0 && max_qty < order.quantity) {
        int32_t cancelled = order.quantity - max_qty;
        p->total_liquidity -= cancelled; // update total liquidity at this price level. imp. it does not happen else where like add_order or full delete.
        order.quantity    = max_qty;
        lob.clock        += Config::PT_CANCEL;
        push_specific_ouch(feed_hq,
            event.agent_tier, event.agent_index, event.symbol,
            SpecificOUCHPayload{ CancelAccepted{ cancel.order_id, max_qty }},
            lob.clock, seq_num, event.sequence_num);
        push_order_cancelled_itch(feed_hq,
            cancel.order_id, order.price , cancelled, order.side,
            event.agent_tier, event.agent_index, event.symbol,
            lob.clock, seq_num, event.sequence_num);
    }
    else if (max_qty == 0) {
        int32_t cancelled = order.quantity;
        lob.clock        += Config::PT_CANCEL;
        lob.delete_order( *p, order_index); // here this inside already updates the total liquidity. we dont need to do here.
        push_specific_ouch(feed_hq,
            event.agent_tier, event.agent_index, event.symbol,
            SpecificOUCHPayload{ CancelAccepted{ cancel.order_id, 0 }},
            lob.clock, seq_num, event.sequence_num);
        push_order_cancelled_itch(feed_hq,
            cancel.order_id, order.price , cancelled, order.side, 
            event.agent_tier, event.agent_index, event.symbol,
            lob.clock, seq_num, event.sequence_num);
    }
    // else: max_qty >= current qty or negative — ignore
}


void Engine::process_update(Event&             event,
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
                replace.old_order_id, Reason::orderId_NOT_Found }},
            lob.clock, seq_num, event.sequence_num);
        return;
    }


    uint32_t order_index  = it->second;
    Order&  old_order  = lob.storage_pool[order_index];
    uint64_t old_price = old_order.price;
    int32_t  old_cancelled = old_order.quantity;
    Order_Side  side = old_order.side;

    if(side == Order_Side::Buy){ /// new update . was a big flaw . we have to have a check
        if(replace.new_price > lob.best_bid()){
            push_specific_ouch(feed_hq,
            event.agent_tier, event.agent_index, event.symbol,
            SpecificOUCHPayload{ ReplaceRejected{
                replace.old_order_id, Reason::invalid_price }},
            lob.clock, seq_num, event.sequence_num);
        return;  
        }
    }
    else{
        if(replace.new_price < lob.best_ask()){
            push_specific_ouch(feed_hq,
            event.agent_tier, event.agent_index, event.symbol,
            SpecificOUCHPayload{ ReplaceRejected{
                replace.old_order_id, Reason::invalid_price }},
            lob.clock, seq_num, event.sequence_num);
            return ;
        }
    }

    // Find price level of old order (same side logic as cancel)
    price_level* p = nullptr;
    if (old_order.side == Order_Side::Sell) p = lob.ask.get_level(old_order.price);
    else p = lob.bid.get_level(old_order.price);
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

    // Delete old, add new . new order goes to tail (loses priority)
    lob.delete_order( *p, order_index);
    lob.clock += Config::PT_CANCEL;     // cost of removing old

    // here lob cant be full , as we cancelled one above. so atleast this one will be added.
    lob.add_order(new_order); 

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

    // Public broadcast after  . replace is public (book state changed)
    push_order_replaced_itch(feed_hq,
        replace.old_order_id, old_price , old_cancelled, 
        new_order.order_id, new_order.price, new_order.quantity,
        new_order.side, event.agent_tier, event.agent_index,
        event.symbol,lob.clock, seq_num, event.sequence_num);
}


