#pragma once
// agents.h
// Agent pool structs — HFT, Retail, Fundamentalist.
// All randomness is seeded from Config::MASTER_SEED so simulation is reproducible.
// Kernel owns agent pools. Agents never update their own state.

#include <cstdint>
#include <vector>
#include <random>
#include "config.h"
#include "events.h"

// ============================================================
// HFT Agent — fast, reacts to every ITCH, tracks open orders
// ============================================================
struct HFTAgent {
    uint32_t  id;
    int32_t   inventory      = 0;
    int64_t   cash           = 0;             // fixed-point (price_scale applied)
    uint64_t  l1;                             // ns, set at init from seed
    uint64_t  l2;                             // ns, set at init from seed
    uint64_t  last_react_ns  = 0;            // timestamp of last reaction
    uint64_t  open_order_ids[8] = {};         // tracks up to 8 open orders
    uint8_t   open_order_count  = 0;
    bool      is_active      = true;

    // strategy params
    uint64_t  join_aggressor_threshold;      // spread in price_scale units above which we join
    uint64_t  penny_jump_offset;             // how many ticks to penny-jump
};

// ============================================================
// Retail Agent — slow, gets broker snapshot only, 1 open order
// ============================================================
struct RetailAgent {
    uint32_t  id;
    int32_t   inventory      = 0;
    int64_t   cash           = 0;
    uint64_t  l1;
    uint64_t  l2;
    uint64_t  last_react_ns  = 0;            // ADDED: sleep-cycle tracking
    uint64_t  open_order_id  = 0;            // 0 = none
    bool      has_open_order = false;
    bool      is_active      = true;

    // strategy params — ZIP-style
    uint64_t  trend_threshold;               // price delta to trigger trade (price_scale)
    bool      is_trend_follower;             // true = trend follow, false = mean revert
    double    noise_factor;                  // multiplier on decision: 0.95 to 1.05
};

// ============================================================
// Fundamentalist Agent — anchors price to fair value
// ============================================================
struct FundamentalistAgent {
    uint32_t  id;
    int32_t   inventory      = 0;
    int64_t   cash           = 0;
    uint64_t  l1;
    uint64_t  l2;
    bool      is_active      = true;

    uint64_t  correction_threshold;         // min deviation from fair price to act (price_scale)
    int32_t   order_size;                   // shares per correction order
};

// ============================================================
// Global agent pools — defined in main.cpp
// ============================================================
extern std::vector<HFTAgent>            hft_pool;
extern std::vector<RetailAgent>         retail_pool;
extern std::vector<FundamentalistAgent> fund_pool;

// ============================================================
// Deterministic initialization using master seed from config.h
// All agents get unique but reproducible l1, l2, and params.
// ============================================================
inline void initialize_agents()
{
    std::mt19937 master_gen(Config::MASTER_SEED);

    // --- HFT ---
    std::uniform_int_distribution<uint64_t> hft_l1(150, 300);      // 150-300 ns
    std::uniform_int_distribution<uint64_t> hft_l2(300, 800);
    std::uniform_int_distribution<uint64_t> hft_spread_thresh(10, 50); // in price_scale ticks

    hft_pool.resize(Config::HFT_COUNT);
    for (uint32_t i = 0; i < Config::HFT_COUNT; i++) {
        hft_pool[i].id                      = i;
        hft_pool[i].l1                      = hft_l1(master_gen);
        hft_pool[i].l2                      = hft_l2(master_gen);
        hft_pool[i].join_aggressor_threshold= hft_spread_thresh(master_gen);
        hft_pool[i].penny_jump_offset       = Config::TICK_SIZE;
        hft_pool[i].cash                    = Config::HFT_INITIAL_CASH;
    }

    // --- Retail ---
    std::uniform_int_distribution<uint64_t> ret_l1(4000000, 6000000); // 4-6 ms
    std::uniform_int_distribution<uint64_t> ret_l2(500000, 2000000);
    std::uniform_int_distribution<uint64_t> ret_thresh(50, 300);      // price_scale
    std::uniform_real_distribution<double>  ret_noise(0.95, 1.05);
    std::bernoulli_distribution             ret_trend(0.6);            // 60% trend followers

    retail_pool.resize(Config::RETAIL_COUNT);
    for (uint32_t i = 0; i < Config::RETAIL_COUNT; i++) {
        retail_pool[i].id               = i;
        retail_pool[i].l1               = ret_l1(master_gen);
        retail_pool[i].l2               = ret_l2(master_gen);
        retail_pool[i].trend_threshold  = ret_thresh(master_gen);
        retail_pool[i].is_trend_follower= ret_trend(master_gen);
        retail_pool[i].noise_factor     = ret_noise(master_gen);
        retail_pool[i].cash             = Config::RETAIL_INITIAL_CASH;
    }

    // --- Fundamentalist ---
    std::uniform_int_distribution<uint64_t> fund_l1(500, 2000);
    std::uniform_int_distribution<uint64_t> fund_l2(500, 1500);
    std::uniform_int_distribution<uint64_t> fund_thresh(200, 1000); // price_scale

    fund_pool.resize(Config::FUND_COUNT);
    for (uint32_t i = 0; i < Config::FUND_COUNT; i++) {
        fund_pool[i].id                    = i;
        fund_pool[i].l1                    = fund_l1(master_gen);
        fund_pool[i].l2                    = fund_l2(master_gen);
        fund_pool[i].correction_threshold  = fund_thresh(master_gen);
        fund_pool[i].order_size            = 100; // 100 shares per correction
        fund_pool[i].cash                  = Config::FUNDAMNTL_INITAL_CASH;
    }
}
