#pragma once
#include "events.h"
#include "config.h"
#include "market_state.h"
#include <random>
#include <cmath>
#include <vector>


// ---------------------------------------------------------
// 1. Calculate AS Price
// ---------------------------------------------------------
double calculate_AS_price(int32_t target_q, Order_Side side, double s, double sigma, double t_factor, double gamma) {
    // 1. Calculate Reservation Price for this specific inventory level
    double r = s - (target_q * gamma * (sigma * sigma) * t_factor);
    
    // 2. Apply a basic half-spread (You can inject the full Kappa math here later if needed)
    // For now, we ensure the MM demands at least a 1-tick edge from their reservation price
    double human_tick = static_cast<double>(Config::TICK_SIZE) / Config::PRICE_SCALE;
    double half_spread = human_tick * 1.0; 
    
    if (side == Order_Side::Buy) {
        return r - half_spread;
    } else {
        return r + half_spread;
    }
}

// ---------------------------------------------------------
// 2. Generate Target Qs (The directional ladder)
// ---------------------------------------------------------
std::vector<std::pair<int32_t, Order_Side>> generate_target_qs(int32_t current_q, int max_layers) {
    std::vector<std::pair<int32_t, Order_Side>> targets;
    int32_t layer_size = 100; // Can be configurable per MM later

    for (int i = 0; i < max_layers; ++i) {
        // ASKS: Project lowering inventory
        targets.push_back({current_q - (i * layer_size), Order_Side::Sell});
        
        // BIDS: Project raising inventory
        targets.push_back({current_q + (i * layer_size), Order_Side::Buy});
    }
    return targets;
}

// ---------------------------------------------------------
// 3. Send Cancel Order
// ---------------------------------------------------------
void send_cancel_order(uint64_t order_id, AgentTier tier, uint32_t agent_id, std::vector<Event>& reaction_queue) {
    Event ouch_event;
    ouch_event.agent_tier = tier;
    ouch_event.agent_index = agent_id;
    ouch_event.event_type = EventType::OUCH;
    
    // max_quantity = 0 means full cancel
    ouch_event.payload = OUCHPayload{ CancelOrder{order_id, 0} }; 
    
    reaction_queue.push_back(ouch_event);
}

// ---------------------------------------------------------
// 4. Send Enter Limit Order
// ---------------------------------------------------------
void send_enter_limit_order(uint64_t order_id, uint64_t price, Order_Side side, int32_t qty, 
                            AgentTier tier, uint32_t agent_id, std::vector<Event>& reaction_queue) {
    Event ouch_event;
    ouch_event.agent_tier = tier;
    ouch_event.agent_index = agent_id;
    ouch_event.event_type = EventType::OUCH;
    
    ouch_event.payload = OUCHPayload{ 
        EnterLimitOrder{order_id, price, qty, side, 0} // time_in_force = 0 (DAY)
    }; 
    
    reaction_queue.push_back(ouch_event);
}

// ---------------------------------------------------------
// 5. Cancel All Rungs
// ---------------------------------------------------------
void cancel_all_rungs(MM_Agent& agent, std::vector<Event>& reaction_queue) {
    for (const auto& rung : agent.active_rungs) {
        send_cancel_order(rung.order_id, AgentTier::HFT, agent.id, reaction_queue);
    }
    agent.active_rungs.clear();
}

// ---------------------------------------------------------
// 6. Build Full Ladder
// ---------------------------------------------------------
// Note: order_id_counter should be a global or passed by reference so it constantly increments
void build_full_ladder(MM_Agent& agent, double current_s, double current_sigma, double t_factor, 
                       std::vector<Event>& reaction_queue, uint64_t& order_id_counter) {
    
    std::vector<std::pair<int32_t, Order_Side>> target_grid = generate_target_qs(agent.inventory, agent.max_layers);
    
    int32_t layer_size = 100; // Matches generate_target_qs

    for (const auto& target : target_grid) {
        
        // Calculate exact price using the up-to-date parameters
        double price_human = calculate_AS_price(target.first, target.second, 
                                                current_s, current_sigma, t_factor, agent.gamma);
        
        // Safety bounds check against mid_price can be added here if needed

        // Convert human double to fixed-point price scale
        uint64_t fixed_price = static_cast<uint64_t>(std::round(price_human * Config::PRICE_SCALE));
        
        uint64_t new_id = ++order_id_counter;
        
        send_enter_limit_order(new_id, fixed_price, target.second, layer_size, 
                               AgentTier::HFT, agent.id, reaction_queue);
                               
        agent.active_rungs.push_back({target.first, target.second, new_id, true});
    }
}





// --- Data Structures ---
struct GridRung {
    int32_t target_q;
    Order_Side side;
    uint64_t order_id;
    bool matched; // The O(1) sweep flag
};

struct MM_Agent {
    uint32_t id;
    int32_t  inventory = 0;
    
    // Latency // from total , we do half.
    uint64_t l1_ns;
    uint64_t l2_ns; 
    
    // AS Model Parameters
    double gamma;          
    double s_estimate;     // EWMA filtered mid-price
    double alpha;          // EWMA sensitivity
    
    // The Exact Anchors you specified
    double s_anchor;       
    double sigma_anchor;
    double t_anchor;
    
    uint64_t requote_sensitivity; // Ticks
    int max_layers;               // How deep they quote
    
    std::vector<GridRung> active_rungs; 
};

// --- Initialization ---
void initialize_mms(std::vector<MM_Agent>& mm_pool, int num_mms, uint64_t initial_mid_price) {
    std::mt19937 gen(Config::MASTER_SEED + 100); 

    std::lognormal_distribution<double> dist_gamma(-2.0, 1.0);  
    std::uniform_real_distribution<double> dist_alpha(0.01, 0.20);
    std::uniform_int_distribution<int> dist_requote(2, 4);

    std::poisson_distribution<int> dist_layers_elite(1); 
    std::poisson_distribution<int> dist_layers_mid(3);   
    std::poisson_distribution<int> dist_layers_slow(7);  

    std::uniform_real_distribution<double> dist_tier(0.0, 1.0);
    
    std::uniform_int_distribution<uint64_t> lat_elite(200, 500);          
    std::uniform_int_distribution<uint64_t> lat_software(1000, 20000);    
    std::uniform_int_distribution<uint64_t> lat_inst(1000000, 50000000);  

    for (int i = 0; i < num_mms; ++i) {
        MM_Agent mm;
        mm.id = i;
        mm.inventory = 0;
        
        // Human double scale
        mm.s_estimate = static_cast<double>(initial_mid_price) / Config::PRICE_SCALE; 
        mm.s_anchor = mm.s_estimate;
        
        double tier_prob = dist_tier(gen);
        uint64_t total_l;

        // The Nasdaq Reality Router
        if (tier_prob < 0.05) { 
            total_l = lat_elite(gen);
            mm.max_layers = std::max(1, dist_layers_elite(gen)); 
            mm.requote_sensitivity = 1; 
        } 
        else if (tier_prob < 0.25) {
            total_l = lat_software(gen);
            mm.max_layers = std::max(2, dist_layers_mid(gen));
            mm.requote_sensitivity = 2;
        } 
        else {
            total_l = lat_inst(gen);
            mm.max_layers = std::max(4, dist_layers_slow(gen));
            mm.requote_sensitivity = dist_requote(gen);
        }

        mm.l1_ns = total_l / 2;
        mm.l2_ns = total_l - mm.l1_ns; 
        mm.gamma = dist_gamma(gen);
        mm.alpha = dist_alpha(gen);

        mm_pool.push_back(mm);
    }
}

// --- The Reaction Loop ---
void mm_react(Event& event, MM_Agent& agent, MarketState& M_State, double current_sigma, double current_T) {
    
    // Time factor: 1.0 at start of day, decaying to 0.0 at market close.
    double t = static_cast<double>(event.timestamp + agent.l1_ns);
    double T = static_cast<double>(Config::MARKET_CLOSE_NS);
    double t_factor = (T - t) / T; 
    if (t_factor < 0.0) t_factor = 0.0; // Safety floor


    bool got_filled = false;

    // 1. Process Fills
    if (auto* fill = std::get_if<FillNotification>(&event.payload)) {
        if (fill->side == Order_Side::Buy) agent.inventory += fill->filled_qty;
        else agent.inventory -= fill->filled_qty;
        got_filled = true;
    }

    // 2. EWMA Filter (Only update on ITCH trades)
    double current_mid_human = static_cast<double>(M_State.mid_price) / Config::PRICE_SCALE;
    agent.s_estimate = (agent.alpha * current_mid_human) + ((1.0 - agent.alpha) * agent.s_estimate);

    // 3. Precise Delta r Calculation
    double r_new = agent.s_estimate - (agent.inventory * agent.gamma * (current_sigma*current_sigma) * current_T);
    double r_old_cond = agent.s_anchor - (agent.inventory * agent.gamma * (agent.sigma_anchor*agent.sigma_anchor) * agent.t_anchor);

    double human_tick = static_cast<double>(Config::TICK_SIZE) / Config::PRICE_SCALE; 

    if (abs(r_new - r_old_cond) >= (human_tick * agent.requote_sensitivity)) {
        
        // Massive Drift: Cancel everything, set new anchors, rebuild full ladder.
        cancel_all_rungs(agent);
        agent.s_anchor = agent.s_estimate;
        agent.sigma_anchor = current_sigma;
        agent.t_anchor = current_T;
        
        build_full_ladder(agent, agent.inventory, agent.max_layers);
    } 
    else if (got_filled) {
        
        // Minor Drift / Partial Fill: Use O(1) Sweep to Patch the Grid
        std::vector<std::pair<int32_t, Order_Side>> target_grid = generate_target_qs(agent.inventory, agent.max_layers);
        
        // Reset flags
        for (auto& rung : agent.active_rungs) rung.matched = false;

        // Mark safe rungs & place missing ones
        for (const auto& target : target_grid) {
            bool found = false;
            for (auto& rung : agent.active_rungs) {
                if (rung.target_q == target.first && rung.side == target.second) {
                    rung.matched = true; found = true; break;
                }
            }
            if (!found) {
                // USING NEWEST PARAMETERS FOR ACCURACY
                double price_human = calculate_AS_price(target.first, target.second, 
                                                        agent.s_estimate, current_sigma, t_factor, agent.gamma);
                
                uint64_t fixed_price = static_cast<uint64_t>(std::round(price_human * Config::PRICE_SCALE));
                
                uint64_t new_id = ++order_id_counter;
                send_enter_limit_order(new_id, fixed_price, target.second, 100, 
                                    AgentTier::HFT, agent.id, reaction_queue);
                                    
                agent.active_rungs.push_back({target.first, target.second, new_id, true});
            }
            // if (!found) {
            //     // Calculate AS price using OLD anchors to stay aligned(maybe we can use new ones to be more good. need to think where that will break)......
            //     double price_human = calculate_AS_price(target.first, target.second, agent.s_anchor, agent.sigma_anchor, agent.t_anchor);
            //     uint64_t fixed_price = static_cast<uint64_t>(std::round(price_human * Config::PRICE_SCALE));
                
            //     uint64_t new_id = generate_order_id();
            //     send_enter_limit_order(new_id, fixed_price, target.second);
            //     agent.active_rungs.push_back({target.first, target.second, new_id, true});
            // }
        }

        // Swap-and-Pop useless deep orders
        for (int i = 0; i < agent.active_rungs.size(); ) {
            if (!agent.active_rungs[i].matched) {
                send_cancel_order(agent.active_rungs[i].order_id);
                agent.active_rungs[i] = agent.active_rungs.back();
                agent.active_rungs.pop_back(); 
            } else {
                ++i;
            }
        }
    }
}