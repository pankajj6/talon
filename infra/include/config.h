#pragma once
#include <cstdint>

namespace Config {

    constexpr uint64_t MASTER_SEED = 42;

    // --------------------------------------------------------
    // Fixed-point price: $1.00 = 10000 internally
    // --------------------------------------------------------
    constexpr uint64_t PRICE_SCALE = 10000;
    constexpr uint64_t TICK_SIZE   = 100;   // 1 cent

    // --------------------------------------------------------
    // Exchange processing times (nanoseconds)
    // --------------------------------------------------------
    constexpr uint64_t PT_BASE        = 5000;
    constexpr uint64_t PT_ORDER_FILL  = 10;
    constexpr uint64_t PT_LEVEL_WALK  = 100;
    constexpr uint64_t PT_ADD_ORDER   = 250;
    constexpr uint64_t PT_CANCEL      = 100;

    // --------------------------------------------------------
    // Simulation time window (ns from midnight)
    // --------------------------------------------------------
    constexpr uint64_t PRE_MARKET_NS   = 25200000000000ULL; // 7:00 AM
    constexpr uint64_t MARKET_OPEN_NS  = 34200000000000ULL; // 9:30 AM
    constexpr uint64_t MARKET_CLOSE_NS = 36000000000000ULL; // 10:00 AM 

    // --------------------------------------------------------
    // Pool sizes
    // --------------------------------------------------------
    constexpr int ORDER_POOL_SIZE = 10000000; 
    constexpr int GLOBAL_SQ_SIZE  = 10000000;
    constexpr int BOOK_MAX_LEVELS = 10000; 

    // --------------------------------------------------------
    // New Agent Populations
    // --------------------------------------------------------
    constexpr uint32_t MM_COUNT    = 100;
    constexpr uint32_t NOISE_COUNT = 5000;
    constexpr uint32_t FUND_COUNT  = 50;

    // --------------------------------------------------------
    // Initial capital (fixed-point)
    // --------------------------------------------------------
    constexpr int64_t MM_INITIAL_CASH   = 0; 
    constexpr int64_t NOISE_INITIAL_CASH= 100000LL * PRICE_SCALE; 
    constexpr int64_t FUND_INITIAL_CASH = 10000000LL * PRICE_SCALE; 

    // --------------------------------------------------------
    // Fair price model (Ornstein-Uhlenbeck)
    // --------------------------------------------------------
    constexpr uint64_t FAIR_PRICE_INITIAL = 1000000; // $100.00
    constexpr double   OU_THETA           = 0.15;
    constexpr double   OU_SIGMA           = 0.05;
    constexpr uint64_t FAIR_PRICE_UPDATE_NS  = 1000000; // 1ms

    // --------------------------------------------------------
    // Bootstrap orders : initial book seeding
    // --------------------------------------------------------
    constexpr uint64_t BOOTSTRAP_BID   = 999500;  // $99.95
    constexpr uint64_t BOOTSTRAP_ASK   = 1000500; // $100.05
    constexpr int32_t  BOOTSTRAP_QTY   = 500;

    // --------------------------------------------------------
    // Tape & Momentum Config
    // --------------------------------------------------------
    constexpr double TAPE_VOLUME_SCALE = 0.001; // ------------------------------------------------- changed from 0.01 t 0.001 
    constexpr double VOLATILITY_LAMBDA = 0.999; // EWMA decay for sigma

    // --------------------------------------------------------
    // Logger
    // --------------------------------------------------------
    constexpr uint64_t LOG_INTERVAL_NS      = 10000000;  
    constexpr uint64_t PNL_LOG_INTERVAL_NS  = 100000000; 

    // --------------------------------------------------------
    // Volatility/fat-tail logging for validation
    // Log returns every N events to compute autocorrelation later
    // --------------------------------------------------------
    constexpr uint64_t RETURNS_LOG_INTERVAL_NS  = 1000000; // 1ms
}