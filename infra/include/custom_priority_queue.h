#pragma once
// ============================================================
// custom_priority_queue.h
// Min-heap priority queue for Event objects.
// Priority: lowest timestamp first. Tiebreak: lowest sequence_num.
//
// CHANGED from original (2-month-old version):
//     replaced with #include "Events.h" : uses the real Event struct
//   - isless() updated to use event.timestamp and event.sequence_num
//     (field names match new Event struct)
//   - Class renamed Event_pipe -> CustomPriorityQueue for clarity
//   - Added empty() method: kernel needs this for batch-push checks
//   - pop() on empty queue: returns default-constructed Event with
//     timestamp=0, sequence_num=0. Caller must check empty() first.
//   - Bug fix in pop() heapify-down loop:
//     Old code checked isless(l_child, r_child) before checking if
//     r_child exists : undefined behaviour when r_child_index >=
//     free_index. Fixed: check r_child exists before using r_child.
//   - Bug fix in pop() heapify-down: the else break at the end only
//     triggered when NEITHER child was smaller than parent, but
//     after a left-child swap the loop continued without re-checking
//     from the new parent position correctly. Restructured to a clean
//     "find min child, compare with parent, swap or break" pattern.
// ============================================================

#include <cstdio>
#include <cstdint>
#include "events.h"   // CHANGED: real Event struct
#include "config.h"   // CHANGED: for GLOBAL_SQ_SIZE constant

// ============================================================
//   constexpr int GLOBAL_SQ_SIZE = 10000000; // 10M events in flight
// Sized for a full trading day with 100k agents. (estimate.)
// ============================================================

// ============================================================
// COMPARISON: lower timestamp = higher priority.
// Equal timestamp: lower sequence_num = higher priority.
// Unchanged logic from original, updated field names only.
// ============================================================
inline bool isless(const Event& a, const Event& b)
{
    // field names updated to match new Event struct
    if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
    return a.sequence_num < b.sequence_num;
}


class CustomPriorityQueue 
{
private:
    int   free_index = 0;

  
    // Using a plain array for cache locality 
    Event arr[Config::GLOBAL_SQ_SIZE];

public:
    CustomPriorityQueue() {}

    // --------------------------------------------------------
    // empty(): kernel uses this before popping and in batch loops.
    // CHANGED: new method, did not exist in original.
    // --------------------------------------------------------
    bool empty() const { return free_index == 0; }

    void push(Event e);
    
    Event pop(); 
    
    // --------------------------------------------------------
    // size(): useful for monitoring queue depth (probe layer).
    // CHANGED: new method.
    // --------------------------------------------------------
    int size() const { return free_index; }

    void printarr() const
    {
        for (int i = 0; i < free_index; i++) {
            printf("%lu\n", (unsigned long)arr[i].sequence_num);
        }
    }
};
