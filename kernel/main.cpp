// kernel/main.cpp

#include <iostream>
#include <deque>
#include "events.h"
#include "engine.h"
#include "lob.h"
#include "config.h"
#include "status.h"
#include "custom_priority_queue.h"
#include "market_state.h"
#include "agents.h"          
#include "fair_price.h"      
#include "broker_snapshot.h"
#include "react.h" 
#include "logger.h"

// ============================================================
// GLOBALS
// ============================================================
uint64_t seq_number         = 0;
uint64_t available_order_id = 1000000;


std::vector<HFTAgent>            hft_pool;
std::vector<RetailAgent>         retail_pool;
std::vector<FundamentalistAgent> fund_pool;
std::vector<InstitutionalAgent>  inst_pool;


void update_agent_on_fill(AgentTier tier, uint32_t index, const FillNotification& fill)
{
    // ADDED: actual inventory and cash update in fixed-point
    int64_t trade_value = (int64_t)fill.filled_qty * (int64_t)fill.price;
    bool is_buy = (fill.side == Order_Side::Buy);

    if (tier == AgentTier::HFT && index < hft_pool.size()) {
        hft_pool[index].inventory += is_buy ?  fill.filled_qty : -fill.filled_qty;
        hft_pool[index].cash      += is_buy ? -trade_value     :  trade_value;

    // Free the tracking slot if the order is completely filled!
        if (fill.remaining_qty == 0) {
            for (uint8_t i = 0; i < hft_pool[index].open_order_count; i++) {
                if (hft_pool[index].open_order_ids[i] == fill.order_id) {
                    // Swap with the last element and shrink count (O(1) deletion)
                    hft_pool[index].open_order_ids[i] = hft_pool[index].open_order_ids[--hft_pool[index].open_order_count];
                    hft_pool[index].open_order_ids[hft_pool[index].open_order_count] = 0;
                    break;
                }
            }
        }
    }
    else if (tier == AgentTier::RETAIL && index < retail_pool.size()) {
        retail_pool[index].inventory += is_buy ?  fill.filled_qty : -fill.filled_qty;
        retail_pool[index].cash      += is_buy ? -trade_value     :  trade_value;
        if (fill.remaining_qty == 0) retail_pool[index].has_open_order = false;
    }
}

void update_agent_on_cancel(AgentTier tier, uint32_t index, const CancelAccepted& cancel)
{
    
    if (tier == AgentTier::HFT && index < hft_pool.size()) {
        // NEW: Free the tracking slot when the engine confirms the cancel!
        if (cancel.remaining_qty == 0) { 
            for (uint8_t i = 0; i < hft_pool[index].open_order_count; i++) {
                if (hft_pool[index].open_order_ids[i] == cancel.order_id) {
                    hft_pool[index].open_order_ids[i] = hft_pool[index].open_order_ids[--hft_pool[index].open_order_count];
                    hft_pool[index].open_order_ids[hft_pool[index].open_order_count] = 0;
                    break;
                }
            }
        }
    }
    
    if (tier == AgentTier::RETAIL && index < retail_pool.size()) {
        if (cancel.remaining_qty == 0) retail_pool[index].has_open_order = false;
    }
    // (void)cancel;
}

void update_agent_on_resting(AgentTier tier, uint32_t index, const OrderRestingNotification& r)
{
    if (tier == AgentTier::RETAIL && index < retail_pool.size()) {
        retail_pool[index].open_order_id  = r.order_id;
        retail_pool[index].has_open_order = true;
    }
    (void)tier; (void)index; (void)r;
}

void update_agent_on_replace(AgentTier tier, uint32_t index, ReplaceAccepted& replace)
{
    (void)tier; (void)index; (void)replace;
}

// ============================================================
// L1 / L2 lookup from agent pools
// ============================================================
uint64_t get_agent_l1(AgentTier tier, uint32_t index)
{
    if (tier == AgentTier::HFT    && index < hft_pool.size())    return hft_pool[index].l1;
    if (tier == AgentTier::RETAIL && index < retail_pool.size()) return retail_pool[index].l1;
    return 1000; // inst is too fast. maybe slow down.
}

uint64_t get_agent_l2(AgentTier tier, uint32_t index)
{
    if (tier == AgentTier::HFT    && index < hft_pool.size())    return hft_pool[index].l2;
    if (tier == AgentTier::RETAIL && index < retail_pool.size()) return retail_pool[index].l2;
    return 1000;
}

void optional_react_specific(AgentTier tier, uint32_t index, const Event& ev,
                             std::deque<Event>& rq, uint64_t& avail_id)
{
   if (tier == AgentTier::HFT && index < hft_pool.size()) {
        MarketState ms(Symbol::AAPL); // Temporary dummy state for now
        hft_react_specific(hft_pool[index], ev, ms, Symbol::AAPL, rq);
    }
    else if (tier == AgentTier::RETAIL && index < retail_pool.size()) {
        // Placeholder: Currently Retail doesn't need to react instantly to a fill, // mistake . we need retail to react. to this instead after sleep , then other itch. (sp_event + 1) react when (sp_event+l1)-lastReact   < 1 (sleep window); else it reacts at t = sp_event+l1 ; when t > sleepTime.    
        // but this safely catches their SPECIFIC_OUCH events for the future.
    }
}

// ============================================================
// push bootstrap limit orders to seed the book
// Kernel pushes these as OUCH events at sim start so LOB has
// initial bid/ask before agents start trading
// ============================================================
void push_bootstrap_orders(CustomPriorityQueue& gsq)
{
    // Bid side : several levels below fair price
    uint64_t bid = Config::BOOTSTRAP_BID;
    for (int i = 0; i < 5; i++, bid -= Config::TICK_SIZE) {
        Event e;
        // e.timestamp        = Config::PRE_MARKET_NS + 1;
        e.timestamp        = Config::MARKET_OPEN_NS - 1; 
        e.sequence_num     = seq_number++;
        e.causal_parent_id = 0;
        e.event_type       = EventType::OUCH;
        e.symbol           = Symbol::AAPL;
        e.agent_tier       = AgentTier::EXCHANGE; // from "exchange" bootstrap agent
        e.agent_index      = 0;
        e.payload = OUCHPayload{ EnterLimitOrder{
            available_order_id++, bid, Config::BOOTSTRAP_QTY,
            Order_Side::Buy, 0
        }};
        gsq.push(e);
    }

    // Ask side
    uint64_t ask = Config::BOOTSTRAP_ASK;
    for (int i = 0; i < 5; i++, ask += Config::TICK_SIZE) {
        Event e;
        // e.timestamp        = Config::PRE_MARKET_NS + 1;
        e.timestamp        = Config::MARKET_OPEN_NS - 1; 
        e.sequence_num     = seq_number++;
        e.causal_parent_id = 0;
        e.event_type       = EventType::OUCH;
        e.symbol           = Symbol::AAPL;
        e.agent_tier       = AgentTier::EXCHANGE;
        e.agent_index      = 0;
        e.payload = OUCHPayload{ EnterLimitOrder{
            available_order_id++, ask, Config::BOOTSTRAP_QTY,
            Order_Side::Sell, 0
        }};
        gsq.push(e);
    }
}

// push a fair price update event
void push_fair_price_event(CustomPriorityQueue& global_sq, uint64_t at_time)
{
    Event e;
    e.timestamp        = at_time; // check if we push at this times (random) or decide some synchro.. with lob clock or what
    e.sequence_num     = seq_number++;
    e.causal_parent_id = 0;
    e.event_type       = EventType::ITCH;
    e.symbol           = Symbol::AAPL;
    e.agent_tier       = AgentTier::EXCHANGE;
    e.agent_index      = 0;
    e.payload = ITCHPayload{FairPriceUpdate{} }; // see events.h addition below
    global_sq.push(e);
}

// ============================================================
// MAIN
// ============================================================
int main()
{
    // initialize agent pools with deterministic seeds
    initialize_agents();

    auto* Global_SQ = new CustomPriorityQueue();
    auto* engine    = new Engine();

    LOB* LOB_appl = new LOB();

    // one MarketState and BrokerSnapshot for AAPL
    MarketState   M_State_appl(Symbol::AAPL);
    BrokerSnapshot broker_snap; // we may need to merge all snaps of diff instruments. so -> bulk data (layers only ,not much details).

    //fair price model
    FairPriceModel fair_model;

    //CSV logger
    SimLogger logger;

    // Seed simulation start
    {
        Event start;
        // start.timestamp        = Config::PRE_MARKET_NS;
        start.timestamp        = Config::MARKET_OPEN_NS;
        start.sequence_num     = seq_number++;
        start.causal_parent_id = 0;
        start.event_type       = EventType::ITCH;
        start.symbol           = Symbol::AAPL;
        start.agent_tier       = AgentTier::EXCHANGE;
        start.agent_index      = 0;
        start.payload          = ITCHPayload{ StartofDay{} };
        Global_SQ->push(start);
    }

    // seed the book with initial orders so HFTs have something to trade against
    push_bootstrap_orders(*Global_SQ);

    // schedule first fair price update at market open
    push_fair_price_event(*Global_SQ, Config::MARKET_OPEN_NS);


    // --- PROGRESS BAR SETUP ---
    uint64_t events_processed = 0;
    uint64_t total_sim_time = Config::MARKET_CLOSE_NS - Config::MARKET_OPEN_NS;
    std::cout << "\nStarting Simulation Engine...\n";

    // ============================================================
    // MAIN LOOP
    // ============================================================
    while (!Global_SQ->empty())
    {
        Event event = Global_SQ->pop();

        // --- PROGRESS BAR LOGIC ---
        events_processed++;
        if (events_processed % 100000 == 0) {
            double progress = 0.0;
            if (event.timestamp >= Config::MARKET_OPEN_NS) {
                progress = (double)(event.timestamp - Config::MARKET_OPEN_NS) / total_sim_time * 100.0;
            }
            
            // \r overwrites the line. std::flush forces it to draw instantly.
            std::cout << "\r[ENGINE RUNNING] LOB Time: " << event.timestamp 
                      << " ns | Progress: " << progress << "% "
                      << "| Events: " << events_processed 
                      << std::flush;
        }

        

        if (event.timestamp == 0 && event.sequence_num == 0) break;

        // stop at market close
        if (event.timestamp > Config::MARKET_CLOSE_NS) break;

        std::deque<Event> reaction_queue;
        std::deque<Event> feed_hq;

        switch (event.event_type)
        {
            // ------------------------------------------------
            // MODE 2  ITCH: update market state, iterate agents
            // ------------------------------------------------
            case EventType::ITCH:
            {
                auto* itch_ptr = std::get_if<ITCHPayload>(&event.payload);
                if (!itch_ptr) break;

                // check if this is a fair price update event
                if (std::get_if<FairPriceUpdate>(itch_ptr)) {
                    fair_model.update(
                        static_cast<double>(Config::FAIR_PRICE_UPDATE_NS) / 1e9);
                    uint64_t fprice = fair_model.get_price();

                    // Wake fundamentalists to correct price if needed
                    for (auto& agent : fund_pool) {
                        fund_react(agent, fprice, M_State_appl, reaction_queue, // we need to pass a general map of M_States that have all lobs M_state . the react function inside decides if what agent interested in which one to react. maybe by using references , agents can access whatever market state they need to think if to by appl or msft . or do see those corelation strategies all.
                                   event.sequence_num);
                    }

                    // Schedule next fair price update
                    push_fair_price_event(*Global_SQ,
                        event.timestamp + Config::FAIR_PRICE_UPDATE_NS);
                    // break; // do not break here , we need to log the fair price .
                }

                if (std::get_if<StartofDay>(itch_ptr)) {
                    // start of day. nothing special yet, agents react below
                }

                // update market state from this ITCH
                update_market_state(*LOB_appl, event, M_State_appl);
                // if it was msft, then pass lob_msft , M_State_msft , in same. so here we will put conditons maybe event.type == appl then call this else that msft or etc..

                // update broker snapshot for retail. again same point is to make broker_snap bulk data.. we just then give reference to it... to retails....
                update_broker_snapshot(broker_snap, M_State_appl);

                // see here we use event.timestamp not lob.clock , as the market state is updated according to when itch comes , so that is the reason
                logger.maybe_log(event.timestamp,
                                 M_State_appl, fair_model.get_price());
                // log PnL for HFT agents
                logger.maybe_log_pnl(event.timestamp);

                // HFT agents react to every ITCH
                for (auto& agent : hft_pool) {
                    hft_react(agent, event, M_State_appl, reaction_queue);
                    // timestamp is set inside the function. using l1+l2. IMP.
                }

                // retail agents react with sleep-cycle filter
                for (auto& agent : retail_pool) {
                    retail_react(agent, event.timestamp, broker_snap, M_State_appl, reaction_queue); // check if we can prevent passing market state , maybe just what is needed.
                }

                for (auto& a : inst_pool) {
                    inst_react(a, event.timestamp, M_State_appl, reaction_queue);
                }

                break;
            }

            // ------------------------------------------------
            // MODE 1  OUCH: call matching engine
            // ------------------------------------------------
            case EventType::OUCH:
            {
                LOB* lob_ptr = nullptr;
                switch (event.symbol) {
                    case Symbol::AAPL: lob_ptr = LOB_appl; break;
                    default: break;
                }
                if (!lob_ptr) break;
                engine->process_ouch_request(event, *lob_ptr, feed_hq, seq_number);
                break;
            }

            // ------------------------------------------------
            // MODE 3  SPECIFIC_OUCH: update one agent, let him react
            // ------------------------------------------------
            case EventType::SPECIFIC_OUCH:
            {
                AgentTier tier  = event.agent_tier;
                uint32_t  index = event.agent_index;

                auto* sp = std::get_if<SpecificOUCHPayload>(&event.payload);
                if (sp) {
                    if (auto* fill  = std::get_if<FillNotification>(sp))
                        update_agent_on_fill(tier, index, *fill);
                    else if (auto* c  = std::get_if<CancelAccepted>(sp))
                        update_agent_on_cancel(tier, index, *c);
                    else if (auto* r  = std::get_if<OrderRestingNotification>(sp))
                        update_agent_on_resting(tier, index, *r);
                    else if (auto* rp = std::get_if<ReplaceAccepted>(sp))
                        update_agent_on_replace(tier, index, *rp);
                
                optional_react_specific(tier, index, event,
                                        reaction_queue, available_order_id);
                
                }

                if (!reaction_queue.empty()) {
                    uint64_t l1 = get_agent_l1(tier, index);
                    uint64_t l2 = get_agent_l2(tier, index);
                    uint64_t rt = event.timestamp + l1 + l2;
                    for (auto& req : reaction_queue) {
                        req.timestamp    = rt;
                        req.sequence_num = seq_number++;
                        Global_SQ->push(req);
                    }
                    reaction_queue.clear();
                }
                break;
            }
        }

        // schedule ITCH agent reactions with correct timestamp
        // reaction_queue from ITCH case - each agent's L1+L2 was baked
        // into the Event by react functions. Kernel just assigns seq_num.
        if (!reaction_queue.empty()) {
            for (auto& r : reaction_queue) {
    
                // old: timestamp was set inside react functions using itch.timestamp + l1 + l2. can have mistakes.

                // we set here . we dont need to trust agents on that .
                auto l1 = get_agent_l1(r.agent_tier,r.agent_index) ; // no cache miss or cold . as most reactions are agent index incre ment wise , as that is howw we iterated for react.
                auto l2 = get_agent_l2(r.agent_tier,r.agent_index) ;
                r.timestamp = event.timestamp + l1 + l2 ;
                r.sequence_num = seq_number++;
                Global_SQ->push(r);
            }
        }

        // Mode 1 output: engine events go straight to queue
        if (!feed_hq.empty()) {
            for (auto& f : feed_hq) {
                Global_SQ->push(f);
            }
        }
    }

    std::cout << "\nSimulation complete. Output: sim_output.csv\n";

    delete Global_SQ;
    delete engine;
    delete LOB_appl;

    return 0;
}
