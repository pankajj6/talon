#pragma once
// ============================================================
// config.h
// All simulation-wide constants in one place.
// Change values here to run different exchange speed scenarios.
// All times are in nanoseconds.
// ============================================================

#include <cstdint>

namespace Config {

    // --- Exchange processing times (nanoseconds) ---
    // PT_BASE: gateway-to-CPU travel + safety checks, applies to every request
    constexpr uint64_t PT_BASE        = 5000;

    // PT_ORDER_FILL: per resting order consumed during matching
    constexpr uint64_t PT_ORDER_FILL  = 10;

    // PT_LEVEL_WALK: added when matching crosses to the next price level
    constexpr uint64_t PT_LEVEL_WALK  = 100;

    // PT_ADD_ORDER: inserting a passive limit order into the book
    constexpr uint64_t PT_ADD_ORDER   = 250;

    // PT_CANCEL: processing a cancel or partial-cancel request
    constexpr uint64_t PT_CANCEL      = 100;

    // PT_REPLACE: replace/update order (price change costs more)
    constexpr uint64_t PT_REPLACE_PRICE = 350;
    constexpr uint64_t PT_REPLACE_QTY   = 100;

    // --- Simulation parameters ---
    constexpr int      ORDER_POOL_SIZE  = 100000;

    // Timestamps in nanoseconds from midnight
    constexpr uint64_t PRE_MARKET_NS    = 25200000000000ULL; // 7:00 AM
    constexpr uint64_t MARKET_OPEN_NS   = 34200000000000ULL; // 9:30 AM
    constexpr uint64_t MARKET_CLOSE_NS  = 57600000000000ULL; // 4:00 PM

    // Price scale and Tick size
    const uint64_t PRICE_SCALE = 10000; // sim world unit.
    const uint64_t TICK_SIZE = 100; // Represents 1/100 of a dollar or $0.01

    // Auction trigger window: if LOB clock is within this many ns of open,
    // engine do not process next ouch if time remaining to auction is less then PT_BASE.
    // trigger batch auction before processing next OUCH
    constexpr uint64_t AUCTION_WINDOW_NS = PT_BASE;

    // Global scheduling queue capacity
    // Sized for a full trading day with 100k agents at high event rate
    constexpr int GLOBAL_SQ_SIZE = 2000000; // 2M events max in flight

    // Retail agents share one broker port — same L3 for all retail
    // L3 < L1 always (direct private port, shorter path than public feed)
    // constexpr uint64_t RETAIL_L3_NS = 50000; // 50 microseconds example

    // --- Agent pool sizes (adjust for your experiment) ---
    constexpr int HFT_COUNT         = 100;
    constexpr int MARKET_MAKER_COUNT = 50;
    constexpr int INSTITUTIONAL_COUNT= 200;
    constexpr int RETAIL_COUNT       = 40000;
}
