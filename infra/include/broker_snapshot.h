#pragma once
// broker_snapshot.h
// Broker snapshot for retail agents.
// Kernel updates this once per ITCH event before iterating retail pool.
// Retail agents receive this : not raw ITCH, not MarketState.

#include <cstdint>
#include "events.h"
#include "base_lob_engine.h"
#include "market_state.h"

struct BrokerSnapshot {
    uint32_t best_bid = 0;
    uint32_t best_ask = 0;
    uint32_t spread = 0;
    uint32_t last_trade_price= 0;
    uint32_t mid_price = 0;
    uint32_t prev_mid_price = 0;   // mid price from previous snapshot: for trend logic
};

// Kernel calls this once per ITCH before retail loop
// only updates from ITCH (broker sees public feed)
inline void update_broker_snapshot(BrokerSnapshot& snap, LOB& lob )
{
    snap.prev_mid_price  = snap.mid_price;
    snap.best_bid = lob.state.best_bid;
    snap.best_ask = lob.state.best_ask;
    snap.spread = lob.state.spread;
    snap.mid_price = lob.state.mid_price;
    snap.last_trade_price = lob.state.last_trade_price; 
    // can add more maybe later..
}
