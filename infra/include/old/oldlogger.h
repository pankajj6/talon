#pragma once
// logger.h
// Periodic CSV logger. Writes market state snapshot to file.
// Uses LOB clock (simulation time), NOT kernel iteration count.
// ADDED: only logs when lob.clock - last_log_time > LOG_INTERVAL_NS

#include <cstdint>
#include <cstdio>
#include "market_state.h"
#include "config.h"

class SimLogger {
private:
    FILE*    file          = nullptr;
    uint64_t last_log_ns   = 0;

public:
    SimLogger(const char* path)
    {
        file = fopen(path, "w");
        if (file) {
            fprintf(file, "sim_time_ns,best_bid,best_ask,mid_price,spread,"
                          "buy_liquidity,sell_liquidity,order_imbalance,"
                          "bid_vol,ask_vol,total_vol\n");
        }
    }

    ~SimLogger() { if (file) fclose(file); }

    // Call this after every ITCH event. Only writes if interval elapsed.
    // ADDED: uses lob_clock (LOB's internal time) not real wall clock
    void maybe_log(uint64_t lob_clock, const MarketState& ms)
    {
        if (file == nullptr) return;
        if (lob_clock - last_log_ns < Config::LOG_INTERVAL_NS) return;
        last_log_ns = lob_clock;

        uint64_t mid = (ms.best_bid > 0 && ms.best_ask > 0)
                       ? (ms.best_bid + ms.best_ask) / 2 : 0;

        fprintf(file, "%llu,%llu,%llu,%llu,%llu,%u,%u,%.6f,%u,%u,%u\n",
            (unsigned long long)lob_clock,
            (unsigned long long)ms.best_bid,
            (unsigned long long)ms.best_ask,
            (unsigned long long)mid,
            (unsigned long long)ms.spread,
            ms.Buy_liquidity,
            ms.Sell_liquidity,
            ms.order_imbalance,
            ms.BidVol,
            ms.AskVol,
            ms.TotalVol);
        fflush(file);
    }
};
