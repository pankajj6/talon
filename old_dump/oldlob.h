#pragma once
// ============================================================
// lob.h
// Limit Order Book and Order Storage Pool.
// One LOB instance per symbol. Engine holds references to them.
//
// CHANGED from lob_structure.txt:
//   - Split into two files conceptually but kept in one header
//     since order_storage_pool must be defined before LOB uses it
//   - order_storage_pool defined BEFORE LOB class (was after,
//     causing "incomplete type" compile error)
//   - uint_64 -> uint64_t throughout (not a valid C++ type)
//   - Status enum removed from here — lives in status.h only
//   - Order struct removed from here — lives in order.h only
//   - #include "order.h" and #include "status.h" added
//   - delete_order: "lob.order_pool[order_index]" ->
//     "storage_pool[order_index]" (delete_order is a method of
//     order_storage_pool, lob is not in scope)
//   - delete_order: "=" -> "==" in two else-if conditions
//     (was silent assignment bug, not comparison)
//   - delete_order: also removes order from lob.orders_by_Id map
//     (was missing — caused stale entries in cancel lookup)
//   - delete_order: parameter uint_64& -> uint32_t (pool indices
//     are int/uint32_t, not uint64_t)
//   - add_order: missing semicolon after ask.emplace() call added
//   - add_order: "storage_pool[p.tail]" -> "storage_pool[p->tail]"
//     (p is a pointer, must use ->)
//   - add_order: "p.tail" -> "p->tail" in all pointer dereferences
//   - LOB::best_bid() and best_ask() return uint64_t to match
//     price type in Order and engine comparisons
//   - LOB::remove_price_level: erases from correct side only,
//     not both (was erasing from bid AND ask regardless of side)
//     Now takes a side parameter.
//   - next_price_level() stub removed (was empty, caused warning)
//   - orders_by_Id map type corrected:
//     unordered_map<int,int> -> unordered_map<uint64_t, uint32_t>
//     (order_id is uint64_t, pool index is uint32_t)
// ============================================================

#include <map>
#include <stack>
#include <functional>
#include <unordered_map>
#include <cstdint>
#include "order.h"    // Order struct
#include "status.h"   // Status enum
#include "config.h"   // POOL_SIZE
#include "market_state.h"

// ============================================================
// price_level: head/tail indices into order_storage_pool.
// -1 means no order at this level.
// ============================================================
struct price_level {
    int head = -1;
    int tail = -1;
};

// Forward declare LOB so order_storage_pool methods can take LOB&
class LOB;

// ============================================================
// order_storage_pool
// CHANGED: defined BEFORE LOB class (was after, causing error).
// Fixed-size array of Orders. Free slots tracked by a stack of
// indices. O(1) alloc and dealloc. Cache-resident for the sim.
// ============================================================
class order_storage_pool
{
public:
    // CHANGED: POOL_SIZE from config.h instead of hardcoded 100000
    Order storage_pool[Config::ORDER_POOL_SIZE];

    std::stack<int> free_index_stack;

    order_storage_pool()
    {
        for (int i = 0; i < Config::ORDER_POOL_SIZE; i++) {
            free_index_stack.push(i);
        }
    }

    // --------------------------------------------------------
    // move_next_order: advance price level head to next order.
    // Used during matching to consume the front resting order.
    // Returns FAILURE if this was the last order on the level.
    // Frees the old head slot back to free_index_stack.
    // --------------------------------------------------------
    Status move_next_order(price_level& orders_list)
    {
        int    head_index  = orders_list.head;
        Order& front_order = storage_pool[head_index];
        int    next_index  = front_order.next;

        // Free the consumed slot
        free_index_stack.push(head_index);

        if (next_index < 0) {
            // Last order on this level — caller must remove price level
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

    // --------------------------------------------------------
    // add_order: insert new_order at tail of its price level.
    // Creates the price level in LOB maps if it doesn't exist.
    // Also registers order_id -> pool_index in orders_by_Id.
    // --------------------------------------------------------
    void add_order(LOB& lob, Order& new_order);
    // (defined after LOB class below — needs full LOB definition)

    // --------------------------------------------------------
    // delete_order: remove an arbitrary order from its level.
    // Handles all four cases: only order, front, back, middle.
    // Frees pool slot and removes from orders_by_Id map.
    //
    // CHANGED: was taking LOB& but accessing lob.order_pool
    // (circular — pool is inside lob). Now uses storage_pool
    // directly. Still takes LOB& to update orders_by_Id map.
    // CHANGED: == fixed (was = assignment bug in two branches).
    // CHANGED: also removes from lob.orders_by_Id.
    // CHANGED: parameter uint_64& -> uint32_t.
    // --------------------------------------------------------
    void delete_order(LOB& lob, price_level& p, uint32_t order_index)
    {
        Order& order = storage_pool[order_index]; // CHANGED: not lob.order_pool

        // Free the pool slot
        free_index_stack.push(order_index);

        int prev_index = order.prev;
        int next_index = order.next;

        if (order.next == -1 && order.prev == -1)
        {
            // Only order on this level
            p.head = -1;
            p.tail = -1;
        }
        else if (order.next == -1 && order.prev != -1) // CHANGED: == not =
        {
            // Back (tail) order deletion
            Order& prev_order = storage_pool[prev_index]; // CHANGED
            prev_order.next   = -1;
            p.tail            = prev_index;
        }
        else if (order.prev == -1 && order.next != -1) // CHANGED: == not =
        {
            // Front (head) order deletion
            Order& next_order = storage_pool[next_index]; // CHANGED
            next_order.prev   = -1;
            p.head            = next_index;
        }
        else
        {
            // Middle order deletion
            Order& next_order = storage_pool[next_index]; // CHANGED
            Order& prev_order = storage_pool[prev_index]; // CHANGED
            next_order.prev   = order.prev;
            prev_order.next   = order.next;
        }

        // CHANGED: remove from id->index map so engine cancel lookup
        // doesn't find stale entries after deletion
        lob.orders_by_Id.erase(order.order_id);
    }

    // --------------------------------------------------------
    // deallocate: just free a slot back to the stack.
    // Used when you have the index but don't need full deletion.
    // --------------------------------------------------------
    void deallocate(int index)
    {
        free_index_stack.push(index);
    }
};


// ============================================================
// LOB — Limit Order Book
// One instance per symbol. Owns its own clock (nanoseconds).
// ============================================================
class LOB
{
public:
    uint64_t clock = 0; // CHANGED: uint_64 -> uint64_t

    // Bid side: descending price order (highest bid first)
    std::map<uint64_t, price_level, std::greater<uint64_t>> bid;

    // Ask side: ascending price order (lowest ask first)
    std::map<uint64_t, price_level> ask;

    // order_id -> index in storage_pool
    // CHANGED: was unordered_map<int,int>, now correct types
    std::unordered_map<uint64_t, uint32_t> orders_by_Id;

    order_storage_pool order_pool;

    // --------------------------------------------------------
    // best_bid / best_ask: return best price or 0 if empty.
    // CHANGED: return uint64_t to match price type in Order.
    // Returning 0 for "no level" is your original design —
    // engine checks != 0 before using.
    // --------------------------------------------------------
    uint64_t best_bid() const
    {
        return bid.empty() ? 0 : bid.begin()->first;
    }

    uint64_t best_ask() const
    {
        return ask.empty() ? 0 : ask.begin()->first;
    }

    // --------------------------------------------------------
    // remove_price_level: erase a price level from one side.
    // CHANGED: original erased from BOTH bid and ask regardless.
    // Now takes a side parameter: 0=bid, 1=ask.
    // Engine always knows which side it's working on.
    // --------------------------------------------------------
    void remove_price_level(uint64_t price, uint8_t side)
    {
        // CHANGED: side-aware erase (was erasing both sides)
        if (side == 0) bid.erase(price);
        else           ask.erase(price);
    }
};


// ============================================================
// add_order: defined here (after LOB) because it needs LOB's
// full definition to access lob.bid, lob.ask, lob.orders_by_Id.
//
// CHANGED: missing semicolon after ask.emplace() added.
// CHANGED: p.tail -> p->tail (p is a pointer).
// CHANGED: registers new order in lob.orders_by_Id map.
// ============================================================
inline void order_storage_pool::add_order(LOB& lob, Order& new_order)
{
    int free_index = free_index_stack.top();
    free_index_stack.pop();

    price_level* p = nullptr;

    if (new_order.side == Order_Side::Buy) // buy
    {
        auto it = lob.bid.find(new_order.price);
        if (it == lob.bid.end()) {
            auto [inserted_it, ok] = lob.bid.emplace(
                new_order.price, price_level{-1, -1});
            p = &inserted_it->second;
        } else {
            p = &it->second;
        }
    }
    else // sell
    {
        auto it = lob.ask.find(new_order.price);
        if (it == lob.ask.end()) {
            auto [inserted_it, ok] = lob.ask.emplace(  // CHANGED: added missing ;
                new_order.price, price_level{-1, -1});
            p = &inserted_it->second;
        } else {
            p = &it->second;
        }
    }

    // Insert at tail of price level
    if (p->head == -1)
    {
        // First order at this price
        p->head           = free_index;
        p->tail           = free_index;
        new_order.next    = -1;
        new_order.prev    = -1;
        storage_pool[free_index] = new_order;
    }
    else
    {
        // Append to tail
        Order& last_order  = storage_pool[p->tail]; // CHANGED: p->tail not p.tail
        last_order.next    = free_index;
        new_order.prev     = p->tail;               // CHANGED: p->tail
        new_order.next     = -1;
        p->tail            = free_index;             // CHANGED: p->tail
        storage_pool[free_index] = new_order;
    }

    // CHANGED: register in id->index map for cancel/replace lookups
    lob.orders_by_Id[new_order.order_id] = static_cast<uint32_t>(free_index);
}
