#pragma once
// logger.h
// Three CSV loggers:
//   1. market_log : mid price, spread, volume (for price path validation)
//   2. pnl_log : HFT realized PnL snapshots.
//   3. returns_log : log returns every 1ms for fat-tail + volatility clustering test
//
// prices are output in human-readable $USD by dividing by PRICE_SCALE
// fair_price column in market_log
// Uses LOB clock (simulation time), NOT wall clock or kernel iteration count.

#include <cstdio>
#include <cstdint>
#include <vector>
#include "market_state.h"
#include "config.h"

// forward declare agent structs
struct HFTAgent;
extern std::vector<HFTAgent> hft_pool;

class SimLogger {
private:
    FILE*    market_file   = nullptr;
    FILE*    pnl_file      = nullptr;
    FILE*    returns_file  = nullptr;

    uint64_t last_market_log_ns  = 0;
    uint64_t last_pnl_log_ns     = 0;
    uint64_t last_returns_log_ns = 0;
    double   last_mid_price      = 0.0;  // for log return calculation

    // helper to convert fixed-point to human-readable double
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

        if (market_file)
            fprintf(market_file,
                "sim_time_ns,mid_price_usd,best_bid_usd,best_ask_usd,spread_usd,"
                "fair_price_usd,buy_liquidity,sell_liquidity,order_imbalance,"
                "bid_vol,ask_vol,total_vol\n");

        if (pnl_file)
            fprintf(pnl_file,
                "sim_time_ns,hft_id,inventory,realized_pnl_usd\n");

        if (returns_file)
            fprintf(returns_file,
                "sim_time_ns,log_return\n");
    }

    ~SimLogger() {
        if (market_file)  fclose(market_file);
        if (pnl_file)     fclose(pnl_file);
        if (returns_file) fclose(returns_file);
    }

    // --------------------------------------------------------
    // Call after every ITCH event : logs market state + returns
    // ADDED: fair_price_fp parameter to log alongside market price
    // --------------------------------------------------------
    void maybe_log(uint64_t lob_clock, const MarketState& ms, uint64_t fair_price_fp)
    {
        if (lob_clock == 0) return;

        // --- market state log ---
        if (market_file && lob_clock - last_market_log_ns >= Config::LOG_INTERVAL_NS) {
            last_market_log_ns = lob_clock;
            double mid = (ms.best_bid > 0 && ms.best_ask > 0)
                         ? to_usd((ms.best_bid + ms.best_ask) / 2) : 0.0;
            fprintf(market_file,
                "%llu,%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%.6f,%u,%u,%u\n",
                (unsigned long long)lob_clock,
                mid,
                to_usd(ms.best_bid),
                to_usd(ms.best_ask),
                to_usd(ms.spread),
                to_usd(fair_price_fp),   // fair price in same USD format
                ms.Buy_liquidity,
                ms.Sell_liquidity,
                ms.order_imbalance,
                ms.bid_executed_shares,
                ms.ask_executed_shares,
                ms.TotalVol);
            fflush(market_file);
        }

        // --- returns log (every 1ms) for fat-tail test ---
        if (returns_file && lob_clock - last_returns_log_ns >= Config::RETURNS_LOG_INTERVAL_NS) {
            last_returns_log_ns = lob_clock;
            if (ms.best_bid > 0 && ms.best_ask > 0) {
                double mid = to_usd((ms.best_bid + ms.best_ask) / 2);
                if (last_mid_price > 0.0) {
                    double log_ret = std::log(mid / last_mid_price);
                    fprintf(returns_file, "%llu,%.8f\n",
                        (unsigned long long)lob_clock, log_ret);
                    fflush(returns_file);
                }
                last_mid_price = mid;
            }
        }
    }

    // --------------------------------------------------------
    // Call periodically for HFT PnL snapshot
    // --------------------------------------------------------
    void maybe_log_pnl(uint64_t lob_clock)
    {

        if (!pnl_file) return;
        if (lob_clock - last_pnl_log_ns < Config::PNL_LOG_INTERVAL_NS) return;
        last_pnl_log_ns = lob_clock;

        for (auto& agent : hft_pool) {
            // MTM PnL = Cash + (Inventory * Current Mid Price)
            double cash_usd = to_usd_signed(agent.cash);
            double inventory_value = (double)agent.inventory * last_mid_price; 
            double mtm_pnl = cash_usd + inventory_value;

            fprintf(pnl_file, "%llu,%u,%d,%.4f\n",
                (unsigned long long)lob_clock,
                agent.id,
                agent.inventory,
                mtm_pnl); 
        }
        fflush(pnl_file);

    //     if (!pnl_file) return;
    //     if (lob_clock - last_pnl_log_ns < Config::PNL_LOG_INTERVAL_NS) return;
    //     last_pnl_log_ns = lob_clock;

    //     for (auto& agent : hft_pool) {
    //         fprintf(pnl_file, "%llu,%u,%d,%.4f\n",
    //             (unsigned long long)lob_clock,
    //             agent.id,
    //             agent.inventory,
    //             to_usd_signed(agent.realized_pnl));
    //     }
    //     fflush(pnl_file);
    }
};
