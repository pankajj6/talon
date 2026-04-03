#include <cstdio>
#include <cstdint>
#include "events.h"  
#include "config.h"
#include "custom_priority_queue.h"


void CustomPriorityQueue::push(Event e)  
{
    
        // Root cause of segfault: hft_react spammed 2 orders per agent per ITCH,
        // each fill created more ITCH, exponential chain exhausted GLOBAL_SQ_SIZE
        if (free_index >= Config::GLOBAL_SQ_SIZE) {
            // silently drop : better than segfault; can increase GLOBAL_SQ_SIZE if this fires
            return;
        }

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


Event CustomPriorityQueue::pop()
{

    if (free_index == 0) return Event{};  // empty: caller checks

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

        // Heapify down: find correct position for new root
        int parent_index = 0;

        while (true)
        {
            int l_index = 2 * parent_index + 1;
            int r_index = l_index + 1;

            if (l_index >= free_index) break; // no children, done

            // find min child safely
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


