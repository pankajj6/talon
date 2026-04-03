#pragma once
// broker_snapshot.h
// Broker snapshot for retail agents.
// Kernel updates this once per ITCH event before iterating retail pool.
// Retail agents receive this : not raw ITCH, not MarketState.

#include <cstdint>
#include "events.h"
#include "market_state.h"

struct BrokerSnapshot {
    uint64_t best_bid        = 0;
    uint64_t best_ask        = 0;
    uint64_t spread          = 0;
    uint64_t last_trade_price= 0;
    uint64_t mid_price       = 0;
    uint64_t prev_mid_price  = 0;   // mid price from previous snapshot: for trend logic
};

// Kernel calls this once per ITCH before retail loop
// only updates from ITCH (broker sees public feed)
inline void update_broker_snapshot(BrokerSnapshot& snap, const MarketState& ms)
{
    snap.prev_mid_price  = snap.mid_price;
    snap.best_bid = ms.best_bid;
    snap.best_ask = ms.best_ask;
    snap.spread = ms.spread;
    snap.mid_price   = ms.mid_price;
    snap.last_trade_price = ms.last_trade_price; 
    // can add more maybe later..
}
