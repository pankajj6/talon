#pragma once
#include <cstdio>
#include <cstdint>
#include "events.h"   // CHANGED: real Event struct
#include "config.h"


inline bool isless(const Event& a, const Event& b)
{
    // CHANGED: field names updated to match new Event struct
    if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
    return a.sequence_num < b.sequence_num;
}




class CustomPriorityQueue  // CHANGED: renamed from Event_pipe
{
private:
    int  free_index = 0;

    // CHANGED: arr[100] -> arr[Config::GLOBAL_SQ_SIZE]
    // 100 was too small for any real simulation run.
    // Using a plain array (no heap allocation) for cache locality —
    // matches your original design intent.
    Event arr[Config::GLOBAL_SQ_SIZE];

public:
    CustomPriorityQueue(){}

    // --------------------------------------------------------
    // empty(): kernel uses this before popping and in batch loops.
    // CHANGED: new method, did not exist in original.
    // --------------------------------------------------------
    bool empty() const { return free_index == 0; }

    void push(Event e);
    
    int size() const { return free_index; }
    
    Event pop(); 
    
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
