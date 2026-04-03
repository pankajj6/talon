// ============================================================
// lob.cpp
// Limit Order Book and Order Storage Pool.
// One LOB instance per symbol. Engine holds references to them.

#include <iostream>
#include <map>
#include <stack>
#include <functional>
#include <unordered_map>
#include <cstdint>
#include "order.h"    // Order struct
#include "status.h"   // Status enum
#include "config.h"   // POOL_SIZE
#include "market_state.h"
#include "lob.h"

Status LOB::move_next_order(price_level& orders_list)// Also means removing current order. then move next.
    {
        int    head_index  = orders_list.head;
        Order& front_order = storage_pool[head_index];
        int    next_index  = front_order.next;

        // Free the consumed slot
        free_index_stack.push(head_index);
        // remove quantity from price level total volume
        orders_list.total_liquidity -= front_order.quantity; // IMP.

        if (next_index < 0) {
            // Last order on this level . caller must remove price level
            return Status::FAILURE;
        }

        // Advance head, clear old front's links
        front_order.next      = -1;
        front_order.prev      = -1;
        orders_list.head      = next_index;

        // New head has no predecessor
        storage_pool[next_index].prev = -1;

        return Status::SUCCESS;
    }

Status LOB::add_order(Order& new_order)
{
    // THE HARD SHIELD:
    if (free_index_stack.empty()) {
        std::cout << "CRITICAL WARNING: LOB Order Pool is FULL! Dropping order.\n";
        return Status::FAILURE; 
    }

    int free_index = free_index_stack.top();
    free_index_stack.pop();

    auto price = new_order.price;

    auto res = (new_order.side == Order_Side::Buy)
           ? bid.get_index_and_level(price)
           : ask.get_index_and_level(price);

    auto idx = res.first;
    auto ptr = res.second;

    if(ptr == nullptr)
    {
        if(new_order.side == Order_Side::Buy)
            ptr = bid.push_level( price_level{-1, -1, 0, 0} ); // get ptr back , for modification.
        else
            ptr = ask.push_level( price_level{-1, -1, 0, 0} );
    }


    // Insert at tail of price level
    if (ptr->head == -1)
    {
        // First order at this price
        ptr->head           = free_index;
        ptr->tail           = free_index;
        ptr->total_liquidity += new_order.quantity; // update total liquidity at this price level
        new_order.next    = -1;
        new_order.prev    = -1;
        storage_pool[free_index] = new_order;
    }
    else
    {
        // Append to tail
        Order& last_order  = storage_pool[ptr->tail];
        last_order.next    = free_index;
        new_order.prev     = ptr->tail;               
        new_order.next     = -1;
        ptr->tail            = free_index;     
        ptr->total_liquidity += new_order.quantity; // update total liquidity at this price level        
        storage_pool[free_index] = new_order;
    }

    // register in id->index map for cancel/replace lookups
    orders_by_Id[new_order.order_id] = static_cast<uint32_t>(free_index);

    return Status::SUCCESS;
}


void LOB::delete_order(price_level& p, uint32_t order_index)
    {
        Order& order = storage_pool[order_index]; 


        // Free the pool slot
        free_index_stack.push(order_index);

        int prev_index = order.prev;
        int next_index = order.next;

        if (order.next == -1 && order.prev == -1)
        {
            // Only order on this level
            p.head = -1;
            p.tail = -1;
            p.total_liquidity = 0; // reset total liquidity when price level is empty

            if(order.side == Order_Side::Buy) bid.erase_level(order.price);
            else ask.erase_level(order.price);

        }
        else if (order.next == -1 && order.prev != -1) 
        {
            // Back (tail) order deletion
            p.total_liquidity -= order.quantity; // update total liquidity at this price level
            Order& prev_order = storage_pool[prev_index];
            prev_order.next   = -1;
            p.tail            = prev_index;
        }
        else if (order.prev == -1 && order.next != -1)
        {
            // Front (head) order deletion
            p.total_liquidity -= order.quantity; // update total liquidity at this price level
            Order& next_order = storage_pool[next_index]; 
            next_order.prev   = -1;
            p.head            = next_index;
        }
        else
        {
            // Middle order deletion
            p.total_liquidity -= order.quantity; // update total liquidity at this price level
            Order& next_order = storage_pool[next_index]; 
            Order& prev_order = storage_pool[prev_index];
            next_order.prev   = order.prev;
            prev_order.next   = order.next;
        }

        // so doesn't find stale entries after deletion
        orders_by_Id.erase(order.order_id);
    }

void LOB::deallocate(int index)
{
    free_index_stack.push(index);
}