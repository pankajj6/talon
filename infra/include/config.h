#pragma once
#include <cstdint>

// ============================================================
// config.h : single source of truth for all simulation parameters
// Change values here to reproduce exact runs or alter behaviour.
// ============================================================

namespace Config {

    constexpr uint64_t order_rate = 1000 ; // per sec , total.
    constexpr uint64_t total_zi = 100; // total zi agents.
    
    // --------------------------------------------------------
    // Reproducibility: change seed to get different but fully
    // reproducible simulation. Same seed = identical run always.
    // --------------------------------------------------------
    constexpr uint64_t MASTER_SEED = 42;

    // --------------------------------------------------------
    // Simulation time window (ns from midnight)
    // --------------------------------------------------------
    constexpr uint64_t PRE_MARKET_NS   = 25200000000000ULL; // 7:00 AM
    constexpr uint64_t MARKET_OPEN_NS  = 34200000000000ULL; // 9:30 AM
    constexpr uint64_t MARKET_CLOSE_NS = 40000000000000ULL; // 10:00 AM //57600000000000ULL; // 4:00 PM

    // --------------------------------------------------------
    // Pool sizes
    // --------------------------------------------------------
    
    constexpr int     GLOBAL_SQ_SIZE  = 10000000;

    // --------------------------------------------------------
    // Maker-taker fee model (fixed-point per share)
    // $0.003/share taker fee = 30 in PRICE_SCALE units
    // $0.002/share maker rebate
    // --------------------------------------------------------
    //constexpr int64_t  TAKER_FEE_PER_SHARE  = 30;   // deducted from taker cash
    //constexpr int64_t  MAKER_REBATE_PER_SHARE= 20;   // added to maker cash
  
    // --------------------------------------------------------
    // Bootstrap orders : initial book seeding
    // --------------------------------------------------------
    constexpr uint32_t BOOTSTRAP_BID   = 999500;  // $99.95
    constexpr uint32_t BOOTSTRAP_ASK   = 1000500; // $100.05
    constexpr int32_t  BOOTSTRAP_QTY   = 500;

    // --------------------------------------------------------
    // SPY bootstrap (different price)
    // --------------------------------------------------------
    constexpr uint64_t SPY_BOOTSTRAP_BID  = 4500000; // $450.00
    constexpr uint64_t SPY_BOOTSTRAP_ASK  = 4500500; // $450.05
    // constexpr uint64_t SPY_FAIR_INITIAL   = 4500000;

    // --------------------------------------------------------
    // Logger: interval in LOB nanoseconds
    // --------------------------------------------------------
    constexpr uint64_t LOG_INTERVAL_NS      = 10000000;  // 10ms market state
    constexpr uint64_t PNL_LOG_INTERVAL_NS  = 100000000; // 100ms PnL snapshot

    // --------------------------------------------------------
    // Volatility/fat-tail logging for validation
    // Log returns every N events to compute autocorrelation later
    // --------------------------------------------------------
    constexpr uint64_t RETURNS_LOG_INTERVAL_NS  = 1000000; // 1ms
}
