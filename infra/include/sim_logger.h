#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include "base_lob_engine.h" // For LOB and LobState

class SimLogger {
private:
    std::ofstream state_file;
    std::ofstream depth_file;
    
    uint64_t log_interval_ns;
    uint64_t next_log_time;
    size_t depth_levels;

public:
    // Default: sample every 10ms (10,000,000 ns), record top 10 levels
    SimLogger(const std::string& state_filename = "sim_output.csv",
              const std::string& depth_filename = "lob_depth.csv",
              uint64_t interval_ns = 10000000, 
              size_t top_n_levels = 10)
        : log_interval_ns(interval_ns), next_log_time(0), depth_levels(top_n_levels) 
    {
        // 1. Initialize State CSV Header
        state_file.open(state_filename);
        if (state_file.is_open()) {
            state_file << "timestamp,best_bid,best_ask,mid_price,spread,"
                       << "last_trade_price,last_trade_size,total_volume,"
                       << "order_imbalance,buy_shares,sell_shares\n";
        }

        // 2. Initialize LOB Depth CSV Header dynamically (bid_p1, bid_v1... ask_p1, ask_v1...)
        depth_file.open(depth_filename);
        if (depth_file.is_open()) {
            depth_file << "timestamp";
            for (size_t i = 1; i <= depth_levels; ++i) {
                depth_file << ",bid_p" << i << ",bid_v" << i;
            }
            for (size_t i = 1; i <= depth_levels; ++i) {
                depth_file << ",ask_p" << i << ",ask_v" << i;
            }
            depth_file << "\n";
        }
    }

    ~SimLogger() {
        if (state_file.is_open()) state_file.close();
        if (depth_file.is_open()) depth_file.close();
    }

    // Unified sampling method: checks clock and writes both files when interval triggers
    void maybe_log(const LOB& parser_lob, uint64_t current_timestamp) {
        // Fast path: skip if interval hasn't elapsed
        if (current_timestamp < next_log_time) return;

        // Initialize timer on first hit
        if (next_log_time == 0) {
            next_log_time = current_timestamp;
        }
        // Advance timer to the next sampling window
        while (next_log_time <= current_timestamp) {
            next_log_time += log_interval_ns;
        }

        const auto& state = parser_lob.state;

        // --- WRITE L1 STATE ---
        if (state_file.is_open()) {
            state_file << current_timestamp << ","
                       << state.best_bid << ","
                       << state.best_ask << ","
                       << state.mid_price << ","
                       << state.spread << ","
                       << state.last_trade_price << ","
                       << state.last_trade_size << ","
                       << state.total_volume << ","
                       << state.order_imbalance << ","
                       << state.buy_shares << ","
                       << state.sell_shares << "\n";
        }

        // --- WRITE L2 DEPTH (For Heatmaps) ---
        if (depth_file.is_open()) {
            depth_file << current_timestamp;

            // Extract Top N Bids (flat_map is ordered std::greater)
            auto bid_it = parser_lob.bid_map.begin();
            for (size_t i = 0; i < depth_levels; ++i) {
                if (bid_it != parser_lob.bid_map.end()) {
                    depth_file << "," << bid_it->first << "," << bid_it->second.total_volume;
                    ++bid_it;
                } else {
                    depth_file << ",0,0"; // Pad with zeros if book has fewer than N levels
                }
            }

            // Extract Top N Asks (flat_map is ordered std::less)
            auto ask_it = parser_lob.ask_map.begin();
            for (size_t i = 0; i < depth_levels; ++i) {
                if (ask_it != parser_lob.ask_map.end()) {
                    depth_file << "," << ask_it->first << "," << ask_it->second.total_volume;
                    ++ask_it;
                } else {
                    depth_file << ",0,0"; 
                }
            }
            depth_file << "\n";
        }
    }
};