#pragma once
// ============================================================
// order.h
// The Order struct that lives inside the order_storage_pool.
// Both lob.h and engine.h include this.
// AgentTier included so engine can update the right agent
// after a passive fill (kernel state-update design).
// ============================================================

#include <cstdint>
#include "events.h"   // for AgentTier enum

struct Order {
    uint64_t  order_id    = 0;
    uint64_t  price       = 0;
    int32_t   quantity    = 0; // we take quantity signed int . it have impact on market state calculations.
    Order_Side side  ;   // 0 = buy, 1 = sell
    AgentTier agent_tier  = AgentTier::RETAIL;
    uint32_t  agent_index = 0;
    int       next        = -1;  // index in storage_pool, -1 = none
    int       prev        = -1;
};