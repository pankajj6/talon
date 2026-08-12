#ifndef LOOKAHEAD_LOGGER_HPP
#define LOOKAHEAD_LOGGER_HPP

#include <fstream>
#include <string>
#include <iomanip>
#include <cmath>

class LookaheadLogger {
private:
    std::ofstream csv_file;
    bool initialized = false;

public:
    LookaheadLogger() = default;

    explicit LookaheadLogger(const std::string& filename) {
        open(filename);
    }

    ~LookaheadLogger() {
        if (csv_file.is_open()) {
            csv_file.flush();
            csv_file.close();
        }
    }

    void open(const std::string& filename) {
        csv_file.open(filename, std::ios::out | std::ios::trunc);
        if (csv_file.is_open()) {
            // Header with both temporal and price/depth state
            csv_file << "event_time,lob_clock,time_diff_ns,"
                     << "shadow_bid,shadow_ask,shadow_mid,"
                     << "exchange_bid,exchange_ask,exchange_mid,"
                     << "price_diff_mid\n";
            initialized = true;
        }
    }

    /**
     * @brief Logs state at the exact instant a reactive agent evaluates an ITCH event.
     * 
     * @param event_time Timestamp of the public ITCH event being processed (Shadow LOB time).
     * @param lob_clock Current exchange matching engine clock time (Exchange LOB time).
     * @param shadow_bid Best bid from shadow LOB.
     * @param shadow_ask Best ask from shadow LOB.
     * @param exchange_bid Best bid from live exchange LOB.
     * @param exchange_ask Best ask from live exchange LOB.
     */
    void log_trigger(uint64_t event_time, 
                     uint64_t lob_clock,
                     double shadow_bid, 
                     double shadow_ask,
                     double exchange_bid, 
                     double exchange_ask) 
    {
        if (!initialized || !csv_file.is_open()) return;

        // Calculate Mid Prices
        double shadow_mid = (shadow_bid + shadow_ask) / 2.0;
        double exchange_mid = (exchange_bid + exchange_ask) / 2.0;

        // Time and Price Discrepancies
        int64_t time_diff_ns = static_cast<int64_t>(lob_clock) - static_cast<int64_t>(event_time);
        double price_diff_mid = exchange_mid - shadow_mid;

        csv_file << event_time << ","
                 << lob_clock << ","
                 << time_diff_ns << ","
                 << std::fixed << std::setprecision(4)
                 << shadow_bid << ","
                 << shadow_ask << ","
                 << shadow_mid << ","
                 << exchange_bid << ","
                 << exchange_ask << ","
                 << exchange_mid << ","
                 << price_diff_mid << "\n";
    }

    void flush() {
        if (csv_file.is_open()) {
            csv_file.flush();
        }
    }
};

#endif // LOOKAHEAD_LOGGER_HPP