#pragma once
// ============================================================
// custom_priority_queue.h
// Min-heap priority queue for Event objects.
// Priority: lowest timestamp first. Tiebreak: lowest sequence_num.
//
// CHANGED from original (2-month-old version):
//   - Old Event struct (with price, order_id, size, agent_id etc)
//     replaced with #include "Events.h" — uses the real Event struct
//   - isless() updated to use event.timestamp and event.sequence_num
//     (field names match new Event struct)
//   - Class renamed Event_pipe -> CustomPriorityQueue for clarity
//   - Fixed-size array arr[100] -> arr[Config::GLOBAL_SQ_SIZE]
//     defined in config.h. Old size of 100 was too small for sim.
//   - pop_nextevent() renamed to pop() to match kernel usage
//   - push_event() renamed to push() to match kernel usage
//   - Added empty() method — kernel needs this for batch-push checks
//   - pop() on empty queue: returns default-constructed Event with
//     timestamp=0, sequence_num=0. Caller must check empty() first.
//   - Bug fix in pop() heapify-down loop:
//     Old code checked isless(l_child, r_child) before checking if
//     r_child exists — undefined behaviour when r_child_index >=
//     free_index. Fixed: check r_child exists before using r_child.
//   - Bug fix in pop() heapify-down: the else break at the end only
//     triggered when NEITHER child was smaller than parent, but
//     after a left-child swap the loop continued without re-checking
//     from the new parent position correctly. Restructured to a clean
//     "find min child, compare with parent, swap or break" pattern.
//   - #include <stdio.h> -> <cstdio>, #include<vector.h> removed
//     (vector.h is not standard; heap uses plain array as before)
// ============================================================

#include <cstdio>
#include <cstdint>
#include "events.h"   // CHANGED: real Event struct
#include "config.h"   // CHANGED: for GLOBAL_SQ_SIZE constant

// ============================================================
// CHANGED: add to config.h if not already there:
//   constexpr int GLOBAL_SQ_SIZE = 2000000; // 2M events in flight
// Sized for a full trading day with 100k agents.
// ============================================================

// ============================================================
// COMPARISON: lower timestamp = higher priority.
// Equal timestamp: lower sequence_num = higher priority.
// Unchanged logic from original, updated field names only.
// ============================================================
inline bool isless(const Event& a, const Event& b)
{
    // CHANGED: field names updated to match new Event struct
    if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
    return a.sequence_num < b.sequence_num;
}


class CustomPriorityQueue  // CHANGED: renamed from Event_pipe
{
private:
    int   free_index = 0;

    // CHANGED: arr[100] -> arr[Config::GLOBAL_SQ_SIZE]
    // 100 was too small for any real simulation run.
    // Using a plain array (no heap allocation) for cache locality —
    // matches your original design intent.
    Event arr[Config::GLOBAL_SQ_SIZE];

public:
    CustomPriorityQueue() {}

    // --------------------------------------------------------
    // empty(): kernel uses this before popping and in batch loops.
    // CHANGED: new method, did not exist in original.
    // --------------------------------------------------------
    bool empty() const { return free_index == 0; }

    // --------------------------------------------------------
    // size(): useful for monitoring queue depth (probe layer).
    // CHANGED: new method.
    // --------------------------------------------------------
    int size() const { return free_index; }

    // --------------------------------------------------------
    // push(): insert event, bubble up to correct position.
    // CHANGED: renamed from push_event(). Logic unchanged.
    // --------------------------------------------------------
    void push(Event e)  // CHANGED: renamed from push_event
    {
        int   child_index = free_index;
        Event child       = e;

        while (true)
        {
            if (child_index == 0)
            {
                arr[child_index] = child;
                break;
            }

            int   parent_index = (child_index - 1) / 2;
            Event parent       = arr[parent_index];

            if (isless(child, parent))
            {
                arr[parent_index] = child;
                arr[child_index]  = parent;
                child_index       = parent_index;
            }
            else
            {
                arr[child_index] = child;
                break;
            }
        }

        free_index++;
    }

    // --------------------------------------------------------
    // pop(): remove and return the highest-priority event.
    // Returns default Event{} if queue is empty — always check
    // empty() before calling.
    //
    // CHANGED: renamed from pop_nextevent().
    //
    // BUG FIX 1: old code did isless(l_child, r_child) before
    // checking if r_child even exists. When r_child_index >=
    // free_index, r_child is uninitialised — undefined behaviour.
    // Fixed: always check r_child_index < free_index before
    // reading r_child.
    //
    // BUG FIX 2: old heapify-down structure had separate if blocks
    // for left and right child that could both execute in one loop
    // iteration, potentially double-swapping. Replaced with a clean
    // "find the minimum child, then compare once with parent" pattern.
    // --------------------------------------------------------
    Event pop()  // CHANGED: renamed from pop_nextevent
    {
        if (free_index == 0) return Event{};  // empty — caller checks

        Event top = arr[0];

        if (free_index == 1)
        {
            arr[0] = Event{};
            free_index--;
            return top;
        }

        // Move last element to root, shrink array
        arr[0] = arr[free_index - 1];
        free_index--;

        // Heapify down — find correct position for new root
        int parent_index = 0;

        while (true)
        {
            int l_index = 2 * parent_index + 1;
            int r_index = l_index + 1;

            if (l_index >= free_index) break; // no children, done

            // CHANGED BUG FIX: find min child safely
            // Check right child exists before comparing
            int min_child_index = l_index;
            if (r_index < free_index &&
                isless(arr[r_index], arr[l_index]))
            {
                min_child_index = r_index;
            }

            // Swap with min child only if child has higher priority
            if (isless(arr[min_child_index], arr[parent_index]))
            {
                Event tmp               = arr[parent_index];
                arr[parent_index]       = arr[min_child_index];
                arr[min_child_index]    = tmp;
                parent_index            = min_child_index;
            }
            else
            {
                break; // parent already has higher priority, stable
            }
        }

        return top;
    }

    // --------------------------------------------------------
    // printarr(): debug helper, prints sequence numbers in
    // heap array order (not priority order).
    // Unchanged from original.
    // --------------------------------------------------------
    void printarr() const
    {
        for (int i = 0; i < free_index; i++) {
            // CHANGED: field name sequence_num (was sequence_num in
            // original too, just verifying it matches new struct)
            printf("%lu\n", (unsigned long)arr[i].sequence_num);
        }
    }
};
