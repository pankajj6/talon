#pragma once
#include <cstdint>

namespace Config {

    // --- Reproducibility ---
    // Change this seed to get a different but fully reproducible simulation
    constexpr uint64_t MASTER_SEED = 42;

    // --- Fixed-point price representation ---
    // $1.00 = 10000 in internal representation
    constexpr uint64_t PRICE_SCALE = 10000;
    constexpr uint64_t TICK_SIZE   = 100;   // 1 cent = 100 in price_scale

    // --- Exchange processing times (nanoseconds) ---
    constexpr uint64_t PT_BASE        = 5000;
    constexpr uint64_t PT_ORDER_FILL  = 10;
    constexpr uint64_t PT_LEVEL_WALK  = 100;
    constexpr uint64_t PT_ADD_ORDER   = 250;
    constexpr uint64_t PT_CANCEL      = 100;
    constexpr uint64_t PT_REPLACE_PRICE = 350;
    constexpr uint64_t PT_REPLACE_QTY   = 100;

    // --- Simulation timestamps (nanoseconds from midnight) ---
    constexpr uint64_t PRE_MARKET_NS   = 25200000000000ULL; // 7:00 AM
    constexpr uint64_t MARKET_OPEN_NS  = 34200000000000ULL; // 9:30 AM
    constexpr uint64_t MARKET_CLOSE_NS = 46800000000000ULL; // 11:00 AM  // 57600000000000 4:00 PM
    constexpr uint64_t AUCTION_WINDOW_NS = 5000;

    // --- Order pool ---
    constexpr int ORDER_POOL_SIZE  = 100000;
    constexpr int GLOBAL_SQ_SIZE   = 2000000;

    // --- Agent counts ---
    constexpr uint32_t HFT_COUNT    = 5;    // start small for testing
    constexpr uint32_t RETAIL_COUNT = 50;   // start small for testing
    constexpr uint32_t FUND_COUNT   = 5;    // fundamentalists

    // --- Agent initial capital (fixed-point) ---
    // $1,000,000 starting cash
    constexpr int64_t  HFT_INITIAL_CASH    = 1000000LL * PRICE_SCALE;
    constexpr int64_t  RETAIL_INITIAL_CASH =  100000LL * PRICE_SCALE;

    // --- Retail sleep cycle ---
    // Retail agents can only react once per this many nanoseconds
    constexpr uint64_t RETAIL_SLEEP_NS = 1000000000ULL; // 1 second

    // --- Fair price model (Ornstein-Uhlenbeck) ---
    // FAIR_PRICE_INITIAL is in fixed-point (e.g. $100.00 = 1000000)
    constexpr uint64_t FAIR_PRICE_INITIAL = 1000000; // $100.00
    constexpr double   OU_THETA           = 0.15;    // mean reversion speed
    constexpr double   OU_SIGMA           = 0.05;    // volatility
    // Fair price update interval (nanoseconds)
    constexpr uint64_t FAIR_PRICE_UPDATE_NS = 1000000; // 1ms

    // --- Bootstrap orders (seed the book at sim start) ---
    // Initial spread: bid at $99.95, ask at $100.05
    constexpr uint64_t BOOTSTRAP_BID = 999500;  // $99.95
    constexpr uint64_t BOOTSTRAP_ASK = 1000500; // $100.05
    constexpr int32_t  BOOTSTRAP_QTY = 1000;    // shares per level

    // --- Logger ---
    // How often to write a CSV row (in nanoseconds of LOB clock)
    constexpr uint64_t LOG_INTERVAL_NS = 10000000; // 10ms
}
