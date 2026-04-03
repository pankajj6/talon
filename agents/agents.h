#pragma once
// agents.h
// All agent pool structs and deterministic seeded initialization.
// Strategy field per agent maps to strategy functions in hft_strategies.h / retail_strategies.h
// HFT has no initial cash (settle at EOD). PnL tracked via realized_pnl.

#include <cstdint>
#include <vector>
#include <random>
#include "config.h"
#include "events.h"

// ============================================================
// HFT Agent
// strategy field drives which function is called in hft_react()
// Some HFTs can switch strategy based on market conditions
// ============================================================
struct HFTAgent {
    uint32_t  id;
    int32_t   inventory       = 0;       // net shares held
    int64_t   realized_pnl    = 0;       // fixed-point, no starting cash
    int64_t   unrealized_pnl  = 0;       // updated on each ITCH if needed
    int64_t   avg_entry_price = 0;       // for pnl calc, fixed-point
    int64_t   cash            = 0;       // for tracking cash changes, not actual constraint // added this. well but mostly hft dont have limt by cash. as settlement happens at market close.

    uint64_t  l1;
    uint64_t  l2;
    uint64_t  last_react_ns   = 0;       // ADDED: HFT cooldown to stop spam

    // open order tracking - HFT cancels old before new
    uint64_t  open_order_ids[16]   = {};
    uint8_t   open_order_sides[16] = {}; // 0=buy 1=sell
    uint8_t   open_order_count     = 0;
    bool      is_active            = true;

    // strategy
    uint8_t   strategy;                  // maps to Config::HFT_STRAT_* constants
    bool      can_switch_strategy;       // if true, agent may change strategy mid-sim

    // strategy-specific params
    uint64_t  join_aggressor_threshold;  // spread threshold (price_scale ticks)
    uint64_t  penny_jump_offset;         // how many ticks to jump (usually TICK_SIZE)

    // fill-flip tracking (David's mechanism)
    uint64_t  last_fill_time      = 0;
    int32_t   fill_flip_inventory = 0;   // inventory at time of passive fill
    uint64_t  fill_flip_price     = 0;   // price at which passive fill occurred
    bool      in_flip_window      = false;

    // spoof tracking
    uint64_t  spoof_order_id      = 0;
    bool      has_spoof_order     = false;
    uint64_t  spoof_placed_time   = 0;

    // quote stuffing
    uint32_t  stuff_burst_count   = 0;
    uint64_t  last_stuff_time     = 0;
};

// ============================================================
// Retail Agent
// CHANGED: strategy field, market order ability
// ============================================================
struct RetailAgent {
    uint32_t  id;
    int32_t   inventory       = 0;
    int64_t   cash;
    int64_t   realized_pnl    = 0;
    int64_t   avg_entry_price = 0;

    uint64_t  l1;
    uint64_t  l2;
    uint64_t  last_react_ns   = 0;       // sleep cycle tracking

    uint64_t  open_order_id   = 0;
    bool      has_open_order  = false;
    bool      is_active       = true;

    uint8_t   strategy;                  // maps to Config::RETAIL_STRAT_* constants
    uint64_t  trend_threshold;
    double    noise_factor;

    //when agent starts trading, for staggered start times. very important for unbaisness towards first itch of second start . as we know agent sleeps for a second. same logic for all configured window.
    uint64_t start_time; 
    // scare params
    uint64_t  scare_price_threshold;     // drop in price_scale that triggers panic
};

// ============================================================
// Fundamentalist Agent
// ============================================================
struct FundamentalistAgent {
    uint32_t  id;
    int32_t   inventory       = 0;
    int64_t   realized_pnl    = 0;

    uint64_t  l1;
    uint64_t  l2;
    bool      is_active       = true;

    uint64_t  correction_threshold;      // min deviation to act
    int32_t   order_size;
};

// ============================================================
// Institutional Agent — large block orders, moves the market
// ============================================================
struct InstitutionalAgent {
    uint32_t  id;
    int32_t   inventory       = 0;
    int64_t   cash;
    int64_t   realized_pnl    = 0;
    int64_t   avg_entry_price = 0;

    uint64_t  l1;
    uint64_t  l2;
    bool      is_active       = true;

    uint8_t   strategy;                  // INST_STRAT_*
    int32_t   target_quantity;           // total shares to buy/sell
    int32_t   remaining_quantity;        // how much left to execute
    uint64_t  slice_size;                // shares per slice (TWAP)
    uint64_t  next_slice_time;           // when to send next slice
    Order_Side direction;                // buying or selling
    uint64_t  entry_limit_price;         // max price willing to pay
};

// ============================================================
// Global agent pools — defined in main.cpp
// ============================================================
extern std::vector<HFTAgent>            hft_pool;
extern std::vector<RetailAgent>         retail_pool;
extern std::vector<FundamentalistAgent> fund_pool;
extern std::vector<InstitutionalAgent>  inst_pool;
// total struct count is ~10Kb,(contiguous for cache).

// ============================================================
// Deterministic seeded initialization
// ============================================================
inline void initialize_agents()
{
    std::mt19937 master_gen(Config::MASTER_SEED);

    // --- HFT ---
    std::uniform_int_distribution<uint64_t> hft_l1(150, 300);
    std::uniform_int_distribution<uint64_t> hft_l2(300, 800);
    std::uniform_int_distribution<uint64_t> hft_spread_thresh(5, 30);
    // distribute strategies across HFT pool
    // strategy order: penny_jump, join_aggressor, fill_flip, market_make, spoof
    const uint8_t hft_strats[] = {
        Config::HFT_STRAT_PENNY_JUMP,
        Config::HFT_STRAT_PENNY_JUMP,
        Config::HFT_STRAT_JOIN_AGGRESSOR,
        Config::HFT_STRAT_FILL_FLIP,
        Config::HFT_STRAT_FILL_FLIP,
        Config::HFT_STRAT_MARKET_MAKE,
        Config::HFT_STRAT_MARKET_MAKE,
        Config::HFT_STRAT_SPOOF
    };

    hft_pool.resize(Config::HFT_COUNT);
    for (uint32_t i = 0; i < Config::HFT_COUNT; i++) {
        hft_pool[i].id                       = i;
        hft_pool[i].l1                       = hft_l1(master_gen);
        hft_pool[i].l2                       = hft_l2(master_gen);
        hft_pool[i].join_aggressor_threshold = hft_spread_thresh(master_gen);
        hft_pool[i].penny_jump_offset        = Config::TICK_SIZE;
        hft_pool[i].cash                     = Config::HFT_INITIAL_CASH;
        hft_pool[i].strategy                 = hft_strats[i % 8];
        hft_pool[i].can_switch_strategy      = (i % 3 == 0); // some agents adaptive
    }
    // OVERRIDE AGENT 0 TO BE THE BERSERKER
    hft_pool[0].strategy = Config::HFT_STRAT_BERSERKER;

    // --- Retail ---
    std::uniform_int_distribution<uint64_t> ret_l1(4000000, 8000000);
    std::uniform_int_distribution<uint64_t> ret_l2(1000000, 3000000);
    std::uniform_int_distribution<uint64_t> ret_thresh(50, 500);
    std::uniform_real_distribution<double>  ret_noise(0.97, 1.03);
    // distribute retail strategies: 40% trend+market, 20% trend+limit, 20% mean revert, 20% noise
    retail_pool.resize(Config::RETAIL_COUNT);
    for (uint32_t i = 0; i < Config::RETAIL_COUNT; i++) {
        retail_pool[i].id               = i;
        retail_pool[i].l1               = ret_l1(master_gen);
        retail_pool[i].l2               = ret_l2(master_gen);
        retail_pool[i].trend_threshold  = ret_thresh(master_gen);
        retail_pool[i].noise_factor     = ret_noise(master_gen);
        retail_pool[i].cash             = Config::RETAIL_INITIAL_CASH;
        retail_pool[i].scare_price_threshold = 500; // $0.05 drop triggers panic 
        uint32_t bucket = i % 5;
        if      (bucket == 0 || bucket == 1) retail_pool[i].strategy = Config::RETAIL_STRAT_TREND_MARKET;
        else if (bucket == 2)                retail_pool[i].strategy = Config::RETAIL_STRAT_TREND_LIMIT;
        else if (bucket == 3)                retail_pool[i].strategy = Config::RETAIL_STRAT_MEAN_REVERT;
        else                                 retail_pool[i].strategy = Config::RETAIL_STRAT_NOISE;
        // recheck below logic later.
        // uint32_t start_time_bucket = Config::RETAIL_START_SCATTER_COUNT; // 5 buckets for staggered start times
        uint32_t scatter_window = (Config::RETAIL_SLEEP_NS)/(Config::RETAIL_START_SCATTER_COUNT);
        uint32_t start_time_bucket = i% Config::RETAIL_START_SCATTER_COUNT;
        retail_pool[i].start_time = Config::MARKET_OPEN_NS + ( start_time_bucket * scatter_window );
        // staggered start times. in a second . is impotant , otherwise all react to same first itch an then go to sleep and then in next second also , only first itch and then sleep . it will be biased.
    }

    // --- Fundamentalist ---
    std::uniform_int_distribution<uint64_t> fund_l1(500, 2000);
    std::uniform_int_distribution<uint64_t> fund_l2(500, 1500);
    std::uniform_int_distribution<uint64_t> fund_thresh(300, 800);

    fund_pool.resize(Config::FUND_COUNT);
    for (uint32_t i = 0; i < Config::FUND_COUNT; i++) {
        fund_pool[i].id                   = i;
        fund_pool[i].l1                   = fund_l1(master_gen);
        fund_pool[i].l2                   = fund_l2(master_gen);
        fund_pool[i].correction_threshold = fund_thresh(master_gen);
        fund_pool[i].order_size           = 200;
    }

    // --- Institutional ---
    std::uniform_int_distribution<uint64_t> inst_l1(10000, 50000);
    std::uniform_int_distribution<uint64_t> inst_l2(50000, 200000);

    inst_pool.resize(Config::INSTITUTIONAL_COUNT);
    for (uint32_t i = 0; i < Config::INSTITUTIONAL_COUNT; i++) {
        inst_pool[i].id               = i;
        inst_pool[i].l1               = inst_l1(master_gen);
        inst_pool[i].l2               = inst_l2(master_gen);
        inst_pool[i].cash             = Config::INSTITUTIONAL_INITIAL_CASH;
        inst_pool[i].target_quantity  = 5000 + (i * 1000);
        inst_pool[i].remaining_quantity= inst_pool[i].target_quantity;
        inst_pool[i].slice_size       = 500;
        inst_pool[i].strategy         = (i % 2 == 0) ? Config::INST_STRAT_BLOCK_BUY
                                                      : Config::INST_STRAT_TWAP;
        inst_pool[i].direction        = (i % 2 == 0) ? Order_Side::Buy : Order_Side::Sell;
        inst_pool[i].entry_limit_price= (i % 2 == 0) ? Config::BOOTSTRAP_ASK + 2000
                                                      : Config::BOOTSTRAP_BID - 2000;
        inst_pool[i].next_slice_time  = Config::MARKET_OPEN_NS + (i * 5000000000ULL);
    }
}
