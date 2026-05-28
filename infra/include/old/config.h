#pragma once
#include <cstdint>

// ============================================================
// config.h : single source of truth for all simulation parameters
// Change values here to reproduce exact runs or alter behaviour.
// ============================================================

namespace Config {

    // --------------------------------------------------------
    // Reproducibility: change seed to get different but fully
    // reproducible simulation. Same seed = identical run always.
    // --------------------------------------------------------
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
    constexpr uint64_t MARKET_CLOSE_NS = 36000000000000ULL; // 10:00 AM //57600000000000ULL; // 4:00 PM

    // --------------------------------------------------------
    // Pool sizes
    // --------------------------------------------------------
    constexpr int     ORDER_POOL_SIZE = 10000000; // 10 M , safe on heap.
    // FIXED: was 2M but hft_react was spamming 2 orders/agent/ITCH
    // causing exponential event chain and OOB write -> segfault.
    // Bounds check added to push(), but also increase size for safety.
    constexpr int     GLOBAL_SQ_SIZE  = 10000000;

    // book size : total price levels.
    constexpr int BOOK_MAX_LEVELS = 500; // each bid ask side get 500-500.

    // --------------------------------------------------------
    // Agent counts: keeping small for initial test, scale up later
    // --------------------------------------------------------
    constexpr uint32_t HFT_COUNT         = 8;
    constexpr uint32_t RETAIL_COUNT      = 100;
    constexpr uint32_t FUND_COUNT        = 5;
    constexpr uint32_t INSTITUTIONAL_COUNT = 3;

    // --------------------------------------------------------
    // Initial capital (fixed-point)
    // HFT: no initial cash. they trade on margin, settle at EOD.
    // PnL only. Retail get fixed capital.
    // --------------------------------------------------------
    constexpr int64_t  HFT_INITIAL_CASH    = 0;  //HFT no cash, PnL only
    constexpr int64_t  RETAIL_INITIAL_CASH   = 100000LL * PRICE_SCALE; // $100k
    constexpr int64_t  INSTITUTIONAL_INITIAL_CASH= 10000000LL * PRICE_SCALE; // $10M
    constexpr int64_t  FUNDAMNTL_INITAL_CASH  = 5000000LL * PRICE_SCALE ; // $5 M // we just need them to have enough powers to move near fundamental price. otherwise we can also take many fundm agents with small cash. its just same behaviour . initally . later we can upgrade accordingly.

    // --------------------------------------------------------
    // Maker-taker fee model (fixed-point per share)
    // $0.003/share taker fee = 30 in PRICE_SCALE units
    // $0.002/share maker rebate
    // --------------------------------------------------------
    constexpr int64_t  TAKER_FEE_PER_SHARE  = 30;   // deducted from taker cash
    constexpr int64_t  MAKER_REBATE_PER_SHARE= 20;   // added to maker cash

    // --------------------------------------------------------
    // Retail sleep cycle: how often retail agents can react
    /* staggered start times. in a second . is impotant , otherwise all react to same first itch an then go to sleep and
    then in next second also , only first itch and then sleep . it will be biased.
    formally: it is important for unbaisness of first itch of second. as we know retail reacts and then sleeps for a second.
    if all retail react to first itch of second and then sleep, then it will be biased towards first itch of second.
    so we need to stagger start times within the first second.(it is stil valid statement for all sleep window config size.)*/
    // --------------------------------------------------------
    constexpr uint64_t RETAIL_SLEEP_NS = 1000000000ULL; // 1 second
    constexpr uint32_t RETAIL_START_SCATTER_COUNT = 10; // 10 buckets for staggered start times


    // --------------------------------------------------------
    // HFT cooldown: prevent order spam, one reaction per window
    // root cause of segfault was no cooldown on hft_react
    // --------------------------------------------------------
    // constexpr uint64_t HFT_COOLDOWN_NS = 50000; // 50 microseconds // then what about quote stuffing ? remove it.

    
    // --------------------------------------------------------
    // Fair price model (Ornstein-Uhlenbeck)
    // update : we will use Heston model.
    // FAIR_PRICE_INITIAL in fixed-point: $100.00 = 1000000
    // --------------------------------------------------------
    constexpr uint64_t FAIR_PRICE_INITIAL    = 1000000; // $100.00
    constexpr double   OU_THETA              = 0.15;
    constexpr double   OU_SIGMA              = 0.05;
    constexpr uint64_t FAIR_PRICE_UPDATE_NS  = 1000000; // 1ms

    // --------------------------------------------------------
    // Bootstrap orders : initial book seeding
    // --------------------------------------------------------
    constexpr uint64_t BOOTSTRAP_BID   = 999500;  // $99.95
    constexpr uint64_t BOOTSTRAP_ASK   = 1000500; // $100.05
    constexpr int32_t  BOOTSTRAP_QTY   = 500;

    // --------------------------------------------------------
    // SPY bootstrap (different price)
    // --------------------------------------------------------
    constexpr uint64_t SPY_BOOTSTRAP_BID  = 4500000; // $450.00
    constexpr uint64_t SPY_BOOTSTRAP_ASK  = 4500500; // $450.05
    constexpr uint64_t SPY_FAIR_INITIAL   = 4500000;

    // --------------------------------------------------------
    // Logger: interval in LOB nanoseconds
    // --------------------------------------------------------
    constexpr uint64_t LOG_INTERVAL_NS      = 10000000;  // 10ms market state
    constexpr uint64_t PNL_LOG_INTERVAL_NS  = 100000000; // 100ms PnL snapshot

    // --------------------------------------------------------
    // Strategy selectors: set which strategies are active.
    // Strategies are defined in agents/strategies/hft_strategies.h
    // --------------------------------------------------------

    // HFT strategy IDs
    constexpr uint8_t HFT_STRAT_PENNY_JUMP      = 0;
    constexpr uint8_t HFT_STRAT_JOIN_AGGRESSOR   = 1;
    constexpr uint8_t HFT_STRAT_FILL_FLIP        = 2;  // evo fill-flip
    constexpr uint8_t HFT_STRAT_QUOTE_STUFF      = 3;  // Nanex quote stuffing
    constexpr uint8_t HFT_STRAT_SPOOF            = 4;  // spoofing + layering
    constexpr uint8_t HFT_STRAT_MARKET_MAKE      = 5;  // two-sided quoting
    
    constexpr uint8_t HFT_STRAT_BERSERKER        = 99; // THE CHAOS AGENT

    // Retail strategy IDs
    constexpr uint8_t RETAIL_STRAT_TREND_LIMIT  = 0;
    constexpr uint8_t RETAIL_STRAT_TREND_MARKET = 1;  // ADDED: 50%+ retail use MO
    constexpr uint8_t RETAIL_STRAT_MEAN_REVERT  = 2;
    constexpr uint8_t RETAIL_STRAT_NOISE        = 3;
    constexpr uint8_t RETAIL_STRAT_SCARE        = 4;  // panic sell on big drop

    // Institutional strategy IDs
    constexpr uint8_t INST_STRAT_BLOCK_BUY      = 0;
    constexpr uint8_t INST_STRAT_BLOCK_SELL     = 1;
    constexpr uint8_t INST_STRAT_TWAP           = 2;

    // --------------------------------------------------------
    // Spoof parameters
    // --------------------------------------------------------
    constexpr int32_t  SPOOF_ORDER_SIZE     = 5000;  // large visible order
    constexpr uint64_t SPOOF_CANCEL_DELAY_NS= 500000; // cancel after 500us
    constexpr uint64_t SPOOF_PRICE_OFFSET   = 300;    // 3 ticks away from best

    // --------------------------------------------------------
    // Fill-flip parameters 
    // F>R threshold: ratio of fill-side flow to trigger flip
    // --------------------------------------------------------
    constexpr double   FILL_FLIP_FLOW_THRESHOLD = 0.6; // 60% same-side fills // should be how much order imbalance is. need to check.
    constexpr uint64_t FILL_FLIP_WINDOW_NS      = 500000; // 500us observation window
    constexpr int32_t  FILL_FLIP_MIN_QTY        = 50;

    // --------------------------------------------------------
    // Quote stuffing parameters
    // --------------------------------------------------------
    constexpr int      QUOTE_STUFF_BURST        = 20;   // orders per burst
    constexpr uint64_t QUOTE_STUFF_INTERVAL_NS  = 100000; // 100us between bursts

    // --------------------------------------------------------
    // Volatility/fat-tail logging for validation
    // Log returns every N events to compute autocorrelation later
    // --------------------------------------------------------
    constexpr uint64_t RETURNS_LOG_INTERVAL_NS  = 1000000; // 1ms
}
