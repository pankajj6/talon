#pragma once
// 
#include "config.h"
#include "events.h" 
#include "market_state.h"
#include <cstdint>
#include <variant>
#include <vector>
#include <random>
#include <cmath>

// ============================================================
// AGENT STRUCTS
// ============================================================

struct GridRung {
    int32_t target_q;
    Order_Side side;
    uint64_t order_id;
    bool matched; // The O(1) sweep flag
};

struct MM_Agent {
    uint32_t id;
    int32_t  inventory = 0;
    int64_t  cash = 0;
    
    uint64_t l1_ns;
    uint64_t l2_ns;
    
    double gamma;          
    double s_estimate;     
    double alpha;          
    
    double s_anchor;       
    double sigma_anchor;
    double t_anchor; // normalised time T-t/T
    
    uint64_t requote_sensitivity; 
    int max_layers;     
    
    int32_t base_size; // new thing . for layer size.
    
    std::vector<GridRung> active_rungs; 
};

struct Noise_Agent {
    uint32_t id;
    uint64_t l1_ns;
    uint64_t l2_ns;
    
    double threshold_x; 
    int32_t base_qty;   
};

struct Fundamentalist_Agent {
    uint32_t id;
    uint64_t l1_ns;
    uint64_t l2_ns;
    
    double error_term;           
    double mispricing_threshold; 
    int32_t base_qty;            
};

// ============================================================
// OUCH REQUEST HELPERS
// ============================================================

inline void send_cancel_order(uint64_t order_id, AgentTier tier, uint32_t agent_id, std::vector<Event>& reaction_queue) {
    Event ouch_event;
    ouch_event.agent_tier = tier;
    ouch_event.agent_index = agent_id;
    ouch_event.event_type = EventType::OUCH;
    ouch_event.payload = OUCHPayload{ CancelOrder{order_id, 0} }; 
    ouch_event.symbol = Symbol::AAPL;
    reaction_queue.push_back(ouch_event);
}

inline void send_enter_limit_order(uint64_t order_id, uint64_t price, Order_Side side, int32_t qty, 
                            AgentTier tier, uint32_t agent_id, std::vector<Event>& reaction_queue) {
    Event ouch_event;
    ouch_event.agent_tier = tier;
    ouch_event.agent_index = agent_id;
    ouch_event.event_type = EventType::OUCH;
    ouch_event.symbol = Symbol::AAPL;
    ouch_event.payload = OUCHPayload{ EnterLimitOrder{order_id, price, qty, side, 0} }; 
    reaction_queue.push_back(ouch_event);
}

inline void send_enter_market_order(uint64_t order_id, Order_Side side, int32_t qty, 
                             AgentTier tier, uint32_t agent_id, std::vector<Event>& reaction_queue) {
    Event ouch_event;
    ouch_event.agent_tier = tier;
    ouch_event.agent_index = agent_id;
    ouch_event.event_type = EventType::OUCH;
    ouch_event.symbol = Symbol::AAPL;
    ouch_event.payload = OUCHPayload{ EnterMarketOrder{order_id, qty, side} }; 
    reaction_queue.push_back(ouch_event);
}

// ============================================================
// MARKET MAKER AS LOGIC
// ============================================================

inline double calculate_AS_price(int32_t target_q, Order_Side side, double s, double sigma, double t_factor, double gamma) {
    
    // 1. STRICT SIGMA CAP: Never let the algorithm think volatility is worse than 0.5% per tick
    double capped_sigma = std::min(0.005, sigma);

    // 2. SANE SCALER: Bring this down from 500k so the base math is stable
    double risk_scale = 100000.0; 
    double scaled_variance = (capped_sigma * capped_sigma) * risk_scale;
    
    // 3. SKEW CAP: An MM will NEVER skew their price by more than $1.00 due to inventory
    double raw_penalty = target_q * gamma * scaled_variance * t_factor;
    double risk_penalty = std::max(-1.0, std::min(1.0, raw_penalty));

    double r = s - risk_penalty;
    double human_tick = static_cast<double>(Config::TICK_SIZE) / Config::PRICE_SCALE;
    
    // 4. SPREAD CAP (THE SAVIOR): An MM will NEVER quote a half-spread wider than $0.50 (50 ticks)
    double raw_half_spread = (gamma * scaled_variance * t_factor) + human_tick;
    double clamped_half_spread = std::min(0.50, raw_half_spread);
    
    return (side == Order_Side::Buy) ? (r - clamped_half_spread) : (r + clamped_half_spread);
}
// inline double calculate_AS_price(int32_t target_q, Order_Side side, double s, double sigma, double t_factor, double gamma) {
    
//     // THE BRIDGE: Scales micro-variance (10^-6) up to human dollars (10^-2). 
//     // You can tune this number. 50,000 to 100,000 is the usual sweet spot. we change to 500k
//     double risk_scale = 500000.0;

//     double scaled_variance = (sigma * sigma) * risk_scale;

//     // Calculate raw penalty
//     double raw_penalty = target_q * gamma * scaled_variance * t_factor;
    
//     // SAFETY CAP: Do not let penalty exceed $50.00 to prevent negative prices
//     double risk_penalty = std::max(-50.0, std::min(50.0, raw_penalty));

//     double r = s - risk_penalty;
    
//     // Now the inventory skew ACTUALLY shifts the reserve price!
//     // double r = s - (target_q * gamma * scaled_variance * t_factor);

//     double human_tick = static_cast<double>(Config::TICK_SIZE) / Config::PRICE_SCALE;

//     // CIRCUIT BREAKER 2: Cap the half_spread expansion to a maximum of $2.00 ------------------------------------------------------------------
//     double raw_half_spread = (gamma * scaled_variance * t_factor) + human_tick;
//     double clamped_half_spread = std::min(2.0, raw_half_spread);
    
//     return (side == Order_Side::Buy) ? (r - clamped_half_spread) : (r + clamped_half_spread);

//     // // Now the spread ACTUALLY widens based on volatility and gamma!
//     // double half_spread = (gamma * scaled_variance * t_factor) + human_tick;
    
//     // return (side == Order_Side::Buy) ? (r - half_spread) : (r + half_spread);
// }

inline std::vector<std::pair<int32_t, Order_Side>> generate_target_qs(int32_t current_q, int max_layers) {
    std::vector<std::pair<int32_t, Order_Side>> targets;
    int32_t layer_size = 50; // look if we can use a distribution . this is the hard wall of liquidity we were thinking.
    for (int i = 0; i < max_layers; ++i) {
        targets.push_back({current_q - (i * layer_size), Order_Side::Sell});
        targets.push_back({current_q + (i * layer_size), Order_Side::Buy});
    }
    return targets;
}

inline void cancel_all_rungs(MM_Agent& agent, std::vector<Event>& reaction_queue) {
    for (const auto& rung : agent.active_rungs) {
        send_cancel_order(rung.order_id, AgentTier::HFT, agent.id, reaction_queue);
    }
    agent.active_rungs.clear();
}

inline void build_full_ladder(MM_Agent& agent, double current_s, double current_sigma, double t_factor, 
                       std::vector<Event>& reaction_queue, uint64_t& order_id_counter) {
    
    auto target_grid = generate_target_qs(agent.inventory, agent.max_layers);
    int32_t layer_size = agent.base_size; // done // i think need to have a distribution.

    for (const auto& target : target_grid) {
        double price_human = calculate_AS_price(target.first, target.second, current_s, current_sigma, t_factor, agent.gamma);
        
        // HARD FLOOR: Prevent 18-Quintillion Underflow!
        price_human = std::max(0.01, price_human);
        
        uint64_t fixed_price = static_cast<uint64_t>(std::round(price_human * Config::PRICE_SCALE));
        
        uint64_t new_id = ++order_id_counter;
        send_enter_limit_order(new_id, fixed_price, target.second, layer_size, AgentTier::HFT, agent.id, reaction_queue);
        agent.active_rungs.push_back({target.first, target.second, new_id, true});
    }
}

// ============================================================
// AGENT REACTIONS
// ============================================================

inline void mm_react(Event& event, MM_Agent& agent, MarketState& M_State, double current_sigma, double t_factor, 
              std::vector<Event>& reaction_queue, uint64_t& order_id_counter) {

    // --- ADD THIS START OF DAY KICKSTART ---
    if (event.event_type == EventType::ITCH) {
        auto* itch_ptr = std::get_if<ITCHPayload>(&event.payload);
        if (itch_ptr && std::holds_alternative<StartofDay>(*itch_ptr)) {
            build_full_ladder(agent, agent.s_anchor, agent.sigma_anchor, agent.t_anchor, reaction_queue, order_id_counter);
            return;
        }
    }
    // ---------------------------------------
    
    bool got_filled = false;

    // 1. Unbox the outer variant (SpecificOUCHPayload)
    if (auto* sp_payload = std::get_if<SpecificOUCHPayload>(&event.payload)) {
        // 2. Unbox the inner variant (FillNotification)
        if (auto* fill = std::get_if<FillNotification>(sp_payload)) {
            
            int64_t trade_value = static_cast<int64_t>(fill->filled_qty) * static_cast<int64_t>(fill->price); // a flaw : fill price is in price scale 10000. solved . it uses to_usd_signed.
            
            if (fill->side == Order_Side::Buy) {
                agent.inventory += fill->filled_qty;
                agent.cash -= trade_value; // PnL tracking
            } else {
                agent.inventory -= fill->filled_qty;
                agent.cash += trade_value; // PnL tracking
            }
            got_filled = true;
        }
    }
    // if (auto* fill = std::get_if<FillNotification>(&event.payload)) {
    //     int64_t trade_value = static_cast<int64_t>(fill->filled_qty) * static_cast<int64_t>(fill->price);
    //     if (fill->side == Order_Side::Buy) {
    //         agent.inventory += fill->filled_qty;
    //         agent.cash -= trade_value;
    //     }
    //     else{ agent.inventory -= fill->filled_qty;
    //         agent.cash += trade_value;
    //     }
    //     got_filled = true;
    // }

    // mid price in human scale .
    // double current_mid_human = static_cast<double>(M_State.mid_price) / Config::PRICE_SCALE;
    
    // if (event.event_type == EventType::ITCH && std::holds_alternative<OrderExecuted>(std::get<ITCHPayload>(event.payload))) {
    //     agent.s_estimate = (agent.alpha * current_mid_human) + ((1.0 - agent.alpha) * agent.s_estimate); // alpha tells: how much he belivie the new info. and how he incorporate it in price.
    // }

    // --- NEW VELOCITY CLAMP LOGIC --- ------------------------------------------------------------------------------------------------------------
    if (event.event_type == EventType::ITCH) {
        if (auto* itch_ptr = std::get_if<ITCHPayload>(&event.payload)) {
            if (auto* executed = std::get_if<OrderExecuted>(itch_ptr)) {
                
                // 1. Trust the Tape: Use the actual execution price, not the broken mid-price
                double trade_price_human = static_cast<double>(executed->price) / Config::PRICE_SCALE;
                
                // 2. Calculate how far the new trade wants to pull our belief
                double desired_shift = (trade_price_human - agent.s_estimate) * agent.alpha;
                
                // 3. VELOCITY CLAMP: Max 10 cents per trade. Prevents teleporting during spikes!
                double clamped_shift = std::max(-0.10, std::min(0.10, desired_shift));
                
                agent.s_estimate += clamped_shift;
            }
        }
    }
    // --------------------------------

    double r_new = agent.s_estimate - (agent.inventory * agent.gamma * (current_sigma*current_sigma) * t_factor);
    double r_old_cond = agent.s_anchor - (agent.inventory * agent.gamma * (agent.sigma_anchor*agent.sigma_anchor) * agent.t_anchor);
    double human_tick = static_cast<double>(Config::TICK_SIZE) / Config::PRICE_SCALE; 

    if (std::abs(r_new - r_old_cond) >= (human_tick * agent.requote_sensitivity)) {
        cancel_all_rungs(agent, reaction_queue);
        agent.s_anchor = agent.s_estimate;
        agent.sigma_anchor = current_sigma;
        agent.t_anchor = t_factor;
        build_full_ladder(agent, agent.s_anchor, agent.sigma_anchor, agent.t_anchor, reaction_queue, order_id_counter);
    } 
    else if (got_filled) {
        auto target_grid = generate_target_qs(agent.inventory, agent.max_layers);
        for (auto& rung : agent.active_rungs) rung.matched = false;

        for (const auto& target : target_grid) {
            bool found = false;
            for (auto& rung : agent.active_rungs) {
                if (rung.target_q == target.first && rung.side == target.second) {
                    rung.matched = true; found = true; break;
                }
            }
            if (!found) {
                // Use up-to-date parameters for replenished order
                double price_human = calculate_AS_price(target.first, target.second, agent.s_estimate, current_sigma, t_factor, agent.gamma);
                
                // HARD FLOOR HERE TOO!
                price_human = std::max(0.01, price_human);
                
                uint64_t fixed_price = static_cast<uint64_t>(std::round(price_human * Config::PRICE_SCALE));
                
                uint64_t new_id = ++order_id_counter;
                send_enter_limit_order(new_id, fixed_price, target.second, agent.base_size, AgentTier::HFT, agent.id, reaction_queue);
                agent.active_rungs.push_back({target.first, target.second, new_id, true});
            }
        }

        // Swap-and-Pop useless deep orders
        for (int i = 0; i < agent.active_rungs.size(); ) {
            if (!agent.active_rungs[i].matched) {
                send_cancel_order(agent.active_rungs[i].order_id, AgentTier::HFT, agent.id, reaction_queue);
                agent.active_rungs[i] = agent.active_rungs.back();
                agent.active_rungs.pop_back(); 
            } else {
                ++i;
            }
        }
    }
}

inline void noise_react(Event& event, Noise_Agent& agent, MarketState& M_State, std::vector<Event>& reaction_queue, uint64_t& order_id_counter) {
    if (event.event_type != EventType::ITCH) return;
    auto* itch_ptr = std::get_if<ITCHPayload>(&event.payload);
    // if (!itch_ptr || !std::holds_alternative<OrderExecuted>(*itch_ptr)) return; // Only react when tape momentum moves

    Order_Side side = (M_State.p_buy > agent.threshold_x) ? Order_Side::Buy : Order_Side::Sell;
    
    send_enter_market_order(++order_id_counter, side, agent.base_qty, AgentTier::RETAIL, agent.id, reaction_queue);
}

inline void fund_react(Event& event, Fundamentalist_Agent& agent, MarketState& M_State, double true_fair_price, 
                std::vector<Event>& reaction_queue, uint64_t& order_id_counter) {
    if (event.event_type != EventType::ITCH) return;

    double human_tick = static_cast<double>(Config::TICK_SIZE) / Config::PRICE_SCALE;
    double true_fair_human = true_fair_price / Config::PRICE_SCALE;
    double estimated_fair = true_fair_human + (agent.error_term * human_tick);
    double current_mid = static_cast<double>(M_State.mid_price) / Config::PRICE_SCALE;
    
    double alpha = estimated_fair - current_mid;
    double required_gap = agent.mispricing_threshold * human_tick;
    
    if (std::abs(alpha) >= required_gap) {
        Order_Side side = (alpha > 0) ? Order_Side::Buy : Order_Side::Sell;
        double severity_multiplier = std::abs(alpha) / required_gap; 
        int32_t final_qty = static_cast<int32_t>(agent.base_qty * severity_multiplier);
        
        send_enter_market_order(++order_id_counter, side, final_qty, AgentTier::Fundamentalist, agent.id, reaction_queue);
    }
}

// ============================================================
// GLOBAL INITIALIZERS
// ============================================================

extern std::vector<MM_Agent> mm_pool;
extern std::vector<Noise_Agent> noise_pool;
extern std::vector<Fundamentalist_Agent> fund_pool;

inline void initialize_mms(uint64_t initial_mid_price) {
    std::mt19937 gen(Config::MASTER_SEED + 100); 
    // std::lognormal_distribution<double> dist_gamma(-2.0, 1.0);
    // std::lognormal_distribution<double> dist_gamma(0.5, 0.5);

    // Mean ~ 4.5, but long right tail. 
    // Generates gammas ranging from 0.5 to 15.0+
    std::lognormal_distribution<double> dist_gamma(1.0, 1.0);  // ---------------------------------------------------------------
    

    std::uniform_real_distribution<double> dist_alpha(0.01, 0.20);
    std::uniform_int_distribution<int> dist_requote(2, 4);

    std::poisson_distribution<int> dist_layers_elite(1); 
    std::poisson_distribution<int> dist_layers_mid(3);   
    std::poisson_distribution<int> dist_layers_slow(7);  

    std::uniform_real_distribution<double> dist_tier(0.0, 1.0);
    std::uniform_int_distribution<uint64_t> lat_elite(200, 500);          
    std::uniform_int_distribution<uint64_t> lat_software(1000, 20000);    
    std::uniform_int_distribution<uint64_t> lat_inst(1000000, 50000000);  


    // NEW: Scatter the initial price beliefs (e.g., +/- 5 cents)
    // std::normal_distribution<double> dist_s_noise(0.0, 0.05); 

    // TIGHTEN THE SCATTER: from 0.05 to 0.01 to prevent initial friendly-fire
    std::normal_distribution<double> dist_s_noise(0.0, 0.01);

    // NEW: Log-normal size distribution (most trade 100, some trade 500+)
    std::lognormal_distribution<double> dist_mm_size(4.6, 0.6);



    for (uint32_t i = 0; i < Config::MM_COUNT; ++i) {
        MM_Agent mm;
        mm.id = i;
        mm.inventory = 0;

        // mm.s_estimate = static_cast<double>(initial_mid_price) / Config::PRICE_SCALE; 
        // mm.s_anchor = mm.s_estimate;

        // Apply the initial disagreement! ------------------------------------------------------------------------------------------------------------
        // double initial_disagreement = dist_s_noise(gen);
        // mm.s_estimate = (static_cast<double>(initial_mid_price) / Config::PRICE_SCALE) + initial_disagreement;
        mm.s_estimate = (static_cast<double>(initial_mid_price) / Config::PRICE_SCALE); 
        mm.s_anchor = mm.s_estimate;

        // Assign unique layer size (rounded to nearest 10 for clean LOB)
        int32_t raw_size = static_cast<int32_t>(dist_mm_size(gen));
        mm.base_size = std::max(10, (raw_size / 10) * 10);
 

        /////////////////////
        
        double tier_prob = dist_tier(gen);
        uint64_t total_l;

        if (tier_prob < 0.05) { 
            total_l = lat_elite(gen);
            mm.max_layers = std::max(1, dist_layers_elite(gen)); 
            mm.requote_sensitivity = 1; 
        } else if (tier_prob < 0.25) {
            total_l = lat_software(gen);
            mm.max_layers = std::max(2, dist_layers_mid(gen));
            mm.requote_sensitivity = 2;
        } else {
            total_l = lat_inst(gen);
            mm.max_layers = std::max(4, dist_layers_slow(gen));
            mm.requote_sensitivity = dist_requote(gen);
        }

        mm.l1_ns = total_l / 2;
        mm.l2_ns = total_l - mm.l1_ns; 
        mm.gamma = dist_gamma(gen);
        mm.alpha = dist_alpha(gen);
        // mm.sigma_anchor = 0.001; -----------------------------------------------------------------------------------------------------------------------------------------
        mm.sigma_anchor = 0.001;
        mm.t_anchor = 1.0;

        mm_pool.push_back(mm);
    }
}

inline void initialize_noise() {
    std::mt19937 gen(Config::MASTER_SEED + 200);
    std::lognormal_distribution<double> dist_latency(19.8, 1.0); 
    std::gamma_distribution<double> gamma_dist(5.0, 1.0); 

    // NEW: Log-normal for volume. Mean is ~50, but tails stretch past 1500.
    std::lognormal_distribution<double> dist_qty_log(3.9, 1.2);

    // std::uniform_int_distribution<int32_t> dist_qty(40, 1000); // changes from 10 to 40 and from 100 to 1000

    for (uint32_t i = 0; i < Config::NOISE_COUNT; ++i) {
        Noise_Agent agent;
        agent.id = i;
        
        uint64_t total_l = static_cast<uint64_t>(dist_latency(gen));
        agent.l1_ns = total_l / 2;
        agent.l2_ns = total_l - agent.l1_ns;
        
        int32_t raw_qty = static_cast<int32_t>(dist_qty_log(gen)); //--------------------------------------------------------------------------------------------
        // Clamp it so we don't accidentally generate a 100,000 share order, round to 10s
        agent.base_qty = std::max(10, std::min(5000, (raw_qty / 10) * 10));

        double g1 = gamma_dist(gen);
        double g2 = gamma_dist(gen);
        agent.threshold_x = g1 / (g1 + g2); 
        // agent.base_qty = dist_qty(gen);
        
        noise_pool.push_back(agent);
    }
}

inline void initialize_fundamentalists() {
    std::mt19937 gen(Config::MASTER_SEED + 300);
    std::lognormal_distribution<double> dist_latency(17.5, 0.8); 
    std::normal_distribution<double> dist_error(0.0, 5.0);
    std::uniform_real_distribution<double> dist_threshold(10.0, 30.0);
    std::uniform_int_distribution<int32_t> dist_qty(500, 2500);

    for (uint32_t i = 0; i < Config::FUND_COUNT; ++i) {
        Fundamentalist_Agent agent;
        agent.id = i;
        
        uint64_t total_l = static_cast<uint64_t>(dist_latency(gen));
        agent.l1_ns = total_l / 2;
        agent.l2_ns = total_l - agent.l1_ns;
        
        agent.error_term = dist_error(gen);
        agent.mispricing_threshold = dist_threshold(gen);
        agent.base_qty = dist_qty(gen);
        
        fund_pool.push_back(agent);
    }
}

inline void initialize_agents() {
    initialize_mms(Config::FAIR_PRICE_INITIAL);
    initialize_noise();
    initialize_fundamentalists();
}

inline uint64_t get_agent_l1(AgentTier tier, uint32_t index) {
    if (tier == AgentTier::HFT && index < mm_pool.size()) return mm_pool[index].l1_ns;
    if (tier == AgentTier::RETAIL && index < noise_pool.size()) return noise_pool[index].l1_ns;
    if (tier == AgentTier::Fundamentalist && index < fund_pool.size()) return fund_pool[index].l1_ns;
    return 1000;
}

inline uint64_t get_agent_l2(AgentTier tier, uint32_t index) {
    if (tier == AgentTier::HFT && index < mm_pool.size()) return mm_pool[index].l2_ns;
    if (tier == AgentTier::RETAIL && index < noise_pool.size()) return noise_pool[index].l2_ns;
    if (tier == AgentTier::Fundamentalist && index < fund_pool.size()) return fund_pool[index].l2_ns;
    return 1000;
}