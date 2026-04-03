#pragma once

#include <stack>
#include <functional>
#include <unordered_map>
#include <cstdint>
#include "order.h"    // Order struct
#include "status.h"   // Status enum
#include "config.h"   // POOL_SIZE

// adding volume at price level for market state updates without walking the list every time.
    // remember we need to update this in cancel in egine specifically , 
    //as they dont call add_order and delete_order , as that is why. becoz
    // it is not full order delete or insert . so update quanity there.



struct price_level {
    int head = -1;
    int tail = -1;
    uint64_t price = 0; // now price will here. use from this directly.
    int64_t total_liquidity = 0; // done. now add logic of this every where .
};


// Side = true for Bids (High to Low), false for Asks (Low to High) // pricemap,true, 500.
template <bool IsBidSide, int MAX_LEVELS = Config::BOOK_MAX_LEVELS>
class PriceMap{

    public:

    price_level level[MAX_LEVELS];
    // end = index_of(last_valid_element) + 1
    int end = 0; 



    // specifically developed for kernel, used in MarketState updation:

    /*pass the  qty to increment(+) or decrement(-) at price. 
     In (+), if adds new level if doesn't exist.
     In (-), delete level  if total_liquidity = 0, after decrement.*/
    void update_by_small_qty(uint64_t price , int32_t small_qty){ 
        // small qty is signed. it can be negative.

        auto* ptr = get_level(price);

        if(ptr == nullptr){ // price level do not exist.
            price_level p = {-1 , -1 , price , small_qty} ; // -1 for head tail . kernel dont care about this . 
            push_level(p);
        }

        else{ // price level exist already . add the new qty in overall.

            ptr->total_liquidity += small_qty ; 

            // in OrderExecuted , OrderCancel , OrderReplace. this is possible
            if(ptr->total_liquidity == 0){ // small_qty was negative , it exhausted the total.
                erase_level(ptr->price); // update by shifting all level left , effectively this level now do not exist.
            }

        }

    }



    // Custom comparator based on side
    bool is_better(uint64_t a, uint64_t b) const {
        if constexpr (IsBidSide) return a > b; // Bids: 101 is better than 100
        else return a < b;                    // Asks: 99 is better than 100
    }

    // for internal use mostly:
    std::pair<int, price_level*> get_index_and_level(uint64_t price) 
    {
        int low = 0, high = end - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;

            if (level[mid].price == price)
                return { mid , &level[mid] };

            if (is_better(level[mid].price, price))
                low = mid + 1;
            else
                high = mid - 1;
        }
        return { end , nullptr};
    }

    auto* get_level(uint64_t price)
    {
        auto[idx , ptr] = get_index_and_level(price);
        return ptr;
    }

    // void push_level(price_level p){
    
    //     auto [idx , ptr] = get_price_level(p.price);

    //     if(idx != end){
    //         return; // already exist. dont push.
    //     }
    //     // here ptr is null ptr

      
    //     if (end < MAX_LEVELS) {
    //         for (int i = end; i > index; i--) {
    //             level[i] = level[i - 1];
    //         }
    //         level[index] = p; // inserted.
    //         end++;
    //     }

    //     }
      
    auto* push_level(price_level p) {
        if (end >= MAX_LEVELS) return; // Book full

        int low = 0, high = end - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (level[mid].price == p.price) return nullptr; // Already exists, skip. do not return the ptr of what exist. can be wrongly modified.

            if (is_better(level[mid].price, p.price)) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }

        // 'low' is now the correct insertion index
        int insert_idx = low;

        // Shift everything from insert_idx to the right
        for (int i = end; i > insert_idx; i--) {
            level[i] = level[i - 1];
        }

        level[insert_idx] = p;
        end++;
        return &level[insert_idx]; // return a ptr. used in modification in add_order.
    }


      
    void erase_level(uint64_t price){
        auto [idx , ptr] = get_index_and_level(price);
        if (idx < end) {
            for (int i = idx; i < end - 1; i++) {
                level[i] = level[i + 1];
            }
            end--;
        }
    }



    bool empty() const {
        return end == 0;
    }

    // call this as map.at(price) to get ptr to price level (if exist). else nullptr.
    // price_level* at(uint64_t price){
    //     auto [idx , ptr] = get_price_level(price); // if does not exist then nullptr automatically.
    //     return ptr; // null if not present.
    // }



};


      // // Insert new level (Shift and Insert)
        // if (end < MAX_LEVELS) { // not in book . add it.
        //     for (int i = end; i > index; i--) {
        //         level[i] = level[i - 1];
        //     }
        //     level[index] = p; // inserted.
        //     end++;
        // }
    


      // //  Update existing level
        // if (index < end) {
        //     level[index].total_liquidity = p.total_liquidity;
        //     level[index].head = p.head;
        //     level[index].tail = p.tail;
        //     return;
        // }


    // void print_top() {
    //     if (end > 0) 
    //         std::cout << (IsBidSide ? "Best Bid: " : "Best Ask: ") << level[0].price << "\n";
    // }



class LOB{
  
    friend class Engine;


private:
    uint64_t clock = 0;
    Order storage_pool[Config::ORDER_POOL_SIZE];
    std::stack<int> free_index_stack;

    PriceMap<true> bid;  // High prices first
    PriceMap<false> ask; // Low prices first

    std::unordered_map<uint64_t, uint32_t> orders_by_Id; // this can be millions , so we use unordered map.

public:

    LOB() {
        for (int i = 0; i < Config::ORDER_POOL_SIZE; i++) {
            free_index_stack.push(i);
        }
    }

    auto copy_bid() const{
        return bid;
    }

    auto copy_ask() const{
        return ask;
    }

    Status move_next_order(price_level& orders_list);
    Status add_order(Order& new_order);
    void delete_order(price_level& p, uint32_t order_index);
    void deallocate(int index);

    //standard lob method:
    uint64_t best_bid() const {return bid.empty() ? 0 : bid.level[0].price;}
    uint64_t best_ask() const {return ask.empty() ? 0 : ask.level[0].price;}

    // void remove_price_level(uint64_t price){
    //     if (side == Order_Side::Buy) bid.erase_level(price);
    //     else ask.erase_level(price);
    // }

};