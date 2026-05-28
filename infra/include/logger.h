#pragma once
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cmath>
#include "market_state.h"
#include "config.h"

// Forward declare the new agent struct
struct MM_Agent;
extern std::vector<MM_Agent> mm_pool;

class SimLogger {
private:
    FILE* market_file   = nullptr;
    FILE* pnl_file      = nullptr;
    FILE* returns_file  = nullptr;
    FILE* depth_file    = nullptr; // NEW DEPTH LOGGER

    uint64_t last_market_log_ns  = 0;
    uint64_t last_pnl_log_ns     = 0;
    uint64_t last_returns_log_ns = 0;
    double   last_mid_price      = 0.0;  

    static double to_usd(uint64_t price_fp) {
        return static_cast<double>(price_fp) / static_cast<double>(Config::PRICE_SCALE);
    }
    static double to_usd_signed(int64_t pnl_fp) {
        return static_cast<double>(pnl_fp) / static_cast<double>(Config::PRICE_SCALE);
    }

public:
    SimLogger()
    {
        market_file  = fopen("sim_market.csv", "w");
        pnl_file     = fopen("sim_pnl.csv", "w");
        returns_file = fopen("sim_returns.csv", "w");
        depth_file   = fopen("sim_depth.csv", "w"); // OPEN DEPTH FILE

        if (market_file)
            fprintf(market_file,
                "sim_time_ns,mid_price_usd,best_bid_usd,best_ask_usd,spread_usd,"
                "fair_price_usd,buy_liquidity,sell_liquidity,order_imbalance,"
                "bid_vol,ask_vol,total_vol,p_buy,dynamic_sigma\n");

        if (pnl_file)
            fprintf(pnl_file, "sim_time_ns,mm_id,inventory,mtm_pnl_usd\n");

        if (returns_file)
            fprintf(returns_file, "sim_time_ns,log_return\n");

        // Write the massive depth header
        if (depth_file) {
            fprintf(depth_file, "sim_time_ns");
            for(int i=1; i<=10; ++i) fprintf(depth_file, ",b%d_p,b%d_q", i, i);
            for(int i=1; i<=10; ++i) fprintf(depth_file, ",a%d_p,a%d_q", i, i);
            fprintf(depth_file, "\n");
        }
    }

    ~SimLogger() {
        if (market_file)  fclose(market_file);
        if (pnl_file)     fclose(pnl_file);
        if (returns_file) fclose(returns_file);
        if (depth_file)   fclose(depth_file);
    }

    void maybe_log(uint64_t lob_clock, const MarketState& ms, uint64_t fair_price_fp)
    {
        if (lob_clock == 0) return;

        // --- market state & depth log ---
        if (lob_clock - last_market_log_ns >= Config::LOG_INTERVAL_NS) {
            last_market_log_ns = lob_clock;
            double mid = (ms.best_bid > 0 && ms.best_ask > 0)
                         ? to_usd((ms.best_bid + ms.best_ask) / 2) : 0.0;
               
            if (market_file) {
                fprintf(market_file,
                    "%llu,%.4f,%.4f,%.4f,%.4f,%.4f,%lld,%lld,%.6f,%u,%u,%u,%.4f,%.6f\n",
                    (unsigned long long)lob_clock, mid, to_usd(ms.best_bid), to_usd(ms.best_ask),
                    to_usd(ms.spread), to_usd(fair_price_fp), (long long)ms.Buy_liquidity,   
                    (long long)ms.Sell_liquidity, ms.order_imbalance, ms.bid_executed_shares,
                    ms.ask_executed_shares, ms.TotalVol, ms.p_buy, ms.current_sigma);
                fflush(market_file);
            }

            // --- LOB DEPTH LOGGING ---
            if (depth_file) {
                fprintf(depth_file, "%llu", (unsigned long long)lob_clock);

                // Scan for Top 10 VALID Bids (Skips empty ghost levels)
                int bids_logged = 0;
                for (int i = 0; i < ms.bid_map.end && bids_logged < 10; ++i) {
                    if (ms.bid_map.level[i].price > 0 && ms.bid_map.level[i].total_liquidity > 0) {
                        fprintf(depth_file, ",%.4f,%lld", to_usd(ms.bid_map.level[i].price), (long long)ms.bid_map.level[i].total_liquidity);
                        bids_logged++;
                    }
                }
                while (bids_logged < 10) { // Fill remaining empty slots with 0s
                    fprintf(depth_file, ",0.0000,0");
                    bids_logged++;
                }

                // Scan for Top 10 VALID Asks
                int asks_logged = 0;
                for (int i = 0; i < ms.ask_map.end && asks_logged < 10; ++i) {
                    if (ms.ask_map.level[i].price > 0 && ms.ask_map.level[i].total_liquidity > 0) {
                        fprintf(depth_file, ",%.4f,%lld", to_usd(ms.ask_map.level[i].price), (long long)ms.ask_map.level[i].total_liquidity);
                        asks_logged++;
                    }
                }
                while (asks_logged < 10) { // Fill remaining empty slots with 0s
                    fprintf(depth_file, ",0.0000,0");
                    asks_logged++;
                }
                
                fprintf(depth_file, "\n");
                fflush(depth_file);
            }
        }

        // --- returns log (every 1ms) ---
        if (returns_file && lob_clock - last_returns_log_ns >= Config::RETURNS_LOG_INTERVAL_NS) {
            last_returns_log_ns = lob_clock;
            if (ms.best_bid > 0 && ms.best_ask > 0) {
                double mid = to_usd((ms.best_bid + ms.best_ask) / 2);
                if (last_mid_price > 0.0) {
                    double log_ret = std::log(mid / last_mid_price);
                    fprintf(returns_file, "%llu,%.8f\n", (unsigned long long)lob_clock, log_ret);
                    fflush(returns_file);
                }
                last_mid_price = mid;
            }
        }
    }

    void maybe_log_pnl(uint64_t lob_clock , MarketState& ms)
    {
        if (!pnl_file) return;
        if (lob_clock - last_pnl_log_ns < Config::PNL_LOG_INTERVAL_NS) return;
        last_pnl_log_ns = lob_clock;

        // Changed from hft_pool to mm_pool
        for (auto& agent : mm_pool) {
            double cash_usd = to_usd_signed(agent.cash);
            double mid = 0.0;
        if (ms.best_bid > 0 && ms.best_ask > 0) {
            mid = to_usd((ms.best_bid + ms.best_ask) / 2);
        }

        double inventory_value = agent.inventory * mid;
            
            double mtm_pnl = cash_usd + inventory_value;

            fprintf(pnl_file, "%llu,%u,%d,%.4f\n",
                (unsigned long long)lob_clock,
                agent.id,
                agent.inventory,
                mtm_pnl); 
        }
        fflush(pnl_file);
    }

    // void maybe_log_pnl(uint64_t lob_clock)
    // {
    //     if (!pnl_file) return;
    //     if (lob_clock - last_pnl_log_ns < Config::PNL_LOG_INTERVAL_NS) return;
    //     last_pnl_log_ns = lob_clock;

    //     for (auto& agent : mm_pool) {
    //         double cash_usd = to_usd_signed(agent.cash);
    //         double inventory_value = (double)agent.inventory * last_mid_price; 
    //         double mtm_pnl = cash_usd + inventory_value;

    //         fprintf(pnl_file, "%llu,%u,%d,%.4f\n", (unsigned long long)lob_clock, agent.id, agent.inventory, mtm_pnl); 
    //     }
    //     fflush(pnl_file);
    // }
    
};

// #pragma once
// // logger.h
// // Three CSV loggers:
// //   1. market_log : mid, spread, volume, momentum (p_buy), and dynamic volatility
// //   2. pnl_log : MM realized PnL snapshots.
// //   3. returns_log : log returns every 1ms for fat-tail + volatility clustering test

// #include <cstdio>
// #include <cstdint>
// #include <vector>
// #include <cmath>
// #include "market_state.h"
// #include "config.h"

// // Forward declare the new agent struct
// struct MM_Agent;
// extern std::vector<MM_Agent> mm_pool;

// class SimLogger {
// private:
//     FILE* market_file   = nullptr;
//     FILE* pnl_file      = nullptr;
//     FILE* returns_file  = nullptr;

//     uint64_t last_market_log_ns  = 0;
//     uint64_t last_pnl_log_ns     = 0;
//     uint64_t last_returns_log_ns = 0;
//     double   last_mid_price      = 0.0;  

//     static double to_usd(uint64_t price_fp) {
//         return static_cast<double>(price_fp) / static_cast<double>(Config::PRICE_SCALE);
//     }
//     static double to_usd_signed(int64_t pnl_fp) {
//         return static_cast<double>(pnl_fp) / static_cast<double>(Config::PRICE_SCALE);
//     }

// public:
//     SimLogger()
//     {
//         market_file  = fopen("sim_market.csv", "w");
//         pnl_file     = fopen("sim_pnl.csv", "w");
//         returns_file = fopen("sim_returns.csv", "w");

//         if (market_file)
//             // ADDED: p_buy and dynamic_sigma columns
//             fprintf(market_file,
//                 "sim_time_ns,mid_price_usd,best_bid_usd,best_ask_usd,spread_usd,"
//                 "fair_price_usd,buy_liquidity,sell_liquidity,order_imbalance,"
//                 "bid_vol,ask_vol,total_vol,p_buy,dynamic_sigma\n");

//         if (pnl_file)
//             fprintf(pnl_file,
//                 "sim_time_ns,mm_id,inventory,mtm_pnl_usd\n");

//         if (returns_file)
//             fprintf(returns_file,
//                 "sim_time_ns,log_return\n");
//     }

//     ~SimLogger() {
//         if (market_file)  fclose(market_file);
//         if (pnl_file)     fclose(pnl_file);
//         if (returns_file) fclose(returns_file);
//     }

//     void maybe_log(uint64_t lob_clock, const MarketState& ms, uint64_t fair_price_fp)
//     {
//         if (lob_clock == 0) return;

//         // --- market state log ---
//         if (market_file && lob_clock - last_market_log_ns >= Config::LOG_INTERVAL_NS) {
//             last_market_log_ns = lob_clock;
//             double mid = (ms.best_bid > 0 && ms.best_ask > 0)
//                          ? to_usd((ms.best_bid + ms.best_ask) / 2) : 0.0;
               
//             // CHANGED %u,%u to %lld,%lld for the liquidity columns
//             fprintf(market_file,
//                 "%llu,%.4f,%.4f,%.4f,%.4f,%.4f,%lld,%lld,%.6f,%u,%u,%u,%.4f,%.6f\n",
//                 (unsigned long long)lob_clock,
//                 mid,
//                 to_usd(ms.best_bid),
//                 to_usd(ms.best_ask),
//                 to_usd(ms.spread),
//                 to_usd(fair_price_fp),   
//                 (long long)ms.Buy_liquidity,   // CAST to long long
//                 (long long)ms.Sell_liquidity,  // CAST to long long
//                 ms.order_imbalance,
//                 ms.bid_executed_shares,
//                 ms.ask_executed_shares,
//                 ms.TotalVol,
//                 ms.p_buy,               
//                 ms.current_sigma);
                
//             fflush(market_file);
//         }

//         // --- returns log (every 1ms) ---
//         if (returns_file && lob_clock - last_returns_log_ns >= Config::RETURNS_LOG_INTERVAL_NS) {
//             last_returns_log_ns = lob_clock;
//             if (ms.best_bid > 0 && ms.best_ask > 0) {
//                 double mid = to_usd((ms.best_bid + ms.best_ask) / 2);
//                 if (last_mid_price > 0.0) {
//                     double log_ret = std::log(mid / last_mid_price);
//                     fprintf(returns_file, "%llu,%.8f\n",
//                         (unsigned long long)lob_clock, log_ret);
//                     fflush(returns_file);
//                 }
//                 last_mid_price = mid;
//             }
//         }
//     }

//     void maybe_log_pnl(uint64_t lob_clock , MarketState& ms)
//     {
//         if (!pnl_file) return;
//         if (lob_clock - last_pnl_log_ns < Config::PNL_LOG_INTERVAL_NS) return;
//         last_pnl_log_ns = lob_clock;

//         // Changed from hft_pool to mm_pool
//         for (auto& agent : mm_pool) {
//             double cash_usd = to_usd_signed(agent.cash);
//             double mid = 0.0;
//         if (ms.best_bid > 0 && ms.best_ask > 0) {
//             mid = to_usd((ms.best_bid + ms.best_ask) / 2);
//         }

//         double inventory_value = agent.inventory * mid;
//             
//             double mtm_pnl = cash_usd + inventory_value;

//             fprintf(pnl_file, "%llu,%u,%d,%.4f\n",
//                 (unsigned long long)lob_clock,
//                 agent.id,
//                 agent.inventory,
//                 mtm_pnl); 
//         }
//         fflush(pnl_file);
//     }
// };