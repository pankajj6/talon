#pragma once
//(in development : will be replaced with Heston model)

// fair_price.h
// Ornstein-Uhlenbeck fair price model.
// All internal calculation in double, conversion to fixed-point uint64 only on get_price().
// Hidden from agents: kernel only. Fundamentalists receive it via kernel, not directly.

#include <cstdint>
#include <cmath>
#include <random>
#include "config.h"

class FairPriceModel {
private:
    double mu;        // long-run mean, in DOUBLE price (not fixed-point)
    double theta;     // mean reversion speed
    double sigma;     // volatility
    double current;   // current fair value in double

    std::mt19937                     gen;
    std::normal_distribution<double> dist;

public:
    // seed taken from config so reproducible
    FairPriceModel()
        : mu    (static_cast<double>(Config::FAIR_PRICE_INITIAL) / Config::PRICE_SCALE)
        , theta (Config::OU_THETA)
        , sigma (Config::OU_SIGMA)
        , current(static_cast<double>(Config::FAIR_PRICE_INITIAL) / Config::PRICE_SCALE)
        , gen   (Config::MASTER_SEED + 999999) // separate stream from agent seeds
        , dist  (0.0, 1.0)
    {}

    // dt in seconds (e.g. 0.001 for 1ms update). unit is in second. so 1 ms =  0.001s
    void update(double dt)
    {
        double drift     = theta * (mu - current) * dt;
        double diffusion = sigma * std::sqrt(dt) * dist(gen);
        current += drift + diffusion;
        if (current < 0.01) current = 0.01; // price floor // check here. mistake.
    }

    // Returns price in fixed-point uint64 (multiplied by PRICE_SCALE)
    // conversion happens ONLY here, not earlier preserves cent precision
    uint64_t get_price() const
    {
        double scaled = current * static_cast<double>(Config::PRICE_SCALE);
        if (scaled < 0) scaled = 0;
        return static_cast<uint64_t>(scaled);
    }
};
