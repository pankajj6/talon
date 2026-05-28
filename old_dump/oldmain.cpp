#include <iostream>
#include <deque>
#include "events.h"
#include "engine.h"
#include "lob.h"
#include "config.h"
#include "status.h"
#include "priority_queue.h"
#include "market_state.h"


// Agent pool headers — add as implemented:
// #include "agents/hft_agent.h"
// #include "agents/retail_agent.h"
// #include "agents/market_maker_agent.h"
// #include "agents/institutional_agent.h"

// ============================================================
// GLOBALS


uint64_t seq_number        = 0;
uint64_t available_order_id = 1000000; // start at 1M, safe headroom



void update_agent_on_fill(AgentTier tier, uint32_t index,
                          const FillNotification& fill)
{
    // TODO: switch on tier, fetch from correct pool, update:
    //   if fill.side == 0: pool[index].inventory += fill.filled_qty
    //   else:              pool[index].inventory -= fill.filled_qty
    //   also update outstanding_qty tracker if agent tracks that
    (void)tier; (void)index; (void)fill; // suppress unused warnings
}

// Called for CancelAccepted: agent's resting qty reduced or zeroed.
void update_agent_on_cancel(AgentTier tier, uint32_t index,
                            const CancelAccepted& cancel)
{
    // TODO: update outstanding_qty tracking in agent pool
    (void)tier; (void)index; (void)cancel;
}

// Called for OrderRestingNotification: agent's order now in book.
void update_agent_on_resting(AgentTier tier, uint32_t index,
                             const OrderRestingNotification& resting)
{
    // TODO: record that this agent has a resting order at this price
    (void)tier; (void)index; (void)resting;
}


void update_agent_on_replace(AgentTier tier , uint32_t index , ReplaceAccepted& replace )
{

}



//  come back here . after defin.. struct of hft and market maker.                                                      ++


// ============================================================
// HELPER: get L2 latency for a specific agent
// L2 = time from agent receiving info to OUCH hitting gateway.
//      Includes algorithm think time + network travel.
// TODO: replace stubs with actual pool array lookups.
// ============================================================
uint64_t get_agent_l2(AgentTier tier, uint32_t index)
{
    (void)index;
    switch (tier) {
        case AgentTier::HFT:          return 500;    // ~500 ns
        case AgentTier::MARKET_MAKER: return 1000;
        case AgentTier::INSTITUTIONAL:return 50000;
        case AgentTier::RETAIL:       return 5000000; // ~5 ms
        default:                      return 1000;
    }
}

uint64_t get_agent_l1(AgentTier tier, uint32_t index) // fixed . this was l3 prev . now recheck when assign l1 to all
{
    (void)index;
    switch (tier) {
        case AgentTier::HFT:          return 200;    // ~200 ns, co-located
        case AgentTier::MARKET_MAKER: return 500;
        case AgentTier::INSTITUTIONAL:return 10000;
        // case AgentTier::RETAIL:       return Config::RETAIL_L3_NS;
        default:                      return 1000;
    }
}

// ============================================================
// HELPER: optional_react_specific
// Give ONE specific agent a chance to react to their
// SPECIFIC_OUCH event. Pushes any OUCHs into reaction_queue.
// Timestamp is NOT set here — kernel sets it after this returns
// using: new_ouch.timestamp = specific_ouch.timestamp + L3 + L2
// TODO: implement per-tier logic once agent pools are defined.
// ============================================================


void optional_react_specific(AgentTier tier, uint32_t index, const Event& specific_ouch_event,
                             std::deque<Event>& reaction_queue,
                             uint64_t& available_order_id)
{
    // TODO: switch on tier, fetch agent, call their react function
    // Example structure:
    //
    // switch (tier) {
    //     case AgentTier::HFT: {
    //         auto& agent = hft_pool[index];
    //         agent.react_to_specific(specific_ouch_event,
    //                                 reaction_queue,
    //                                 available_order_id);
    //         break;
    //     }
    //     case AgentTier::RETAIL: { ... }
    //     etc.
    // }
    (void)tier; (void)index;
    (void)specific_ouch_event;
    (void)reaction_queue;
    (void)available_order_id;
}



int main()
{
  
    auto* Global_SQ = new CustomPriorityQueue();

    auto* engine = new Engine();

    // One LOB per symbol - passed by reference into engine
    LOB* LOB_appl = new LOB();
    LOB* LOB_msft = new LOB();
    LOB* LOB_tsla = new LOB();
    LOB* LOB_spy = new LOB();

    MarketState M_State_appl(Symbol::AAPL) ; // this are small so maybe ok on stack.
    MarketState M_State_msft(Symbol::MSFT) ;
    MarketState M_State_tsla(Symbol::TSLA) ;  
    MarketState M_State_spy(Symbol::SPY) ;

    // --- Seed the simulation: push start-of-day ITCH ---
    // This is the first event kernel will pop (Mode 2).
    // All agents react to it and place pre-market orders.
    {
        Event start;
        start.timestamp        = Config::PRE_MARKET_NS; // 7:00 AM in ns . for intial testing we can take it 9:00 AM.  later define BatchAuction function .
        start.sequence_num     = seq_number++;
        start.causal_parent_id = 0;
        start.event_type       = EventType::ITCH;
        start.symbol           = Symbol::AAPL; // TODO: one per symbol or generic
        start.agent_tier       = AgentTier::EXCHANGE;
        start.agent_index      = 0;
        start.payload = ITCHPayload{StartofDay{}} ;
        // TODO: define a StartOfDay payload type or reuse OrderAdded with qty=0
        Global_SQ->push(start);
    }



    while (!Global_SQ->empty())
    {
       
        Event event = Global_SQ->pop(); // recheck this once

        if(event.timestamp == 0 && event.sequence_num == 0) {
            break; // Global_SQ is empty, end simulation
        }
        
        std::deque<Event> reaction_queue; // Mode 2: agent reactions
        std::deque<Event> feed_hq;        // Mode 1: engine output
                                          // (also used by Mode 3)

        // --------------------------------------------------------
        // DISPATCH
        // --------------------------------------------------------
        switch (event.event_type)
        {
            
            
            case EventType::ITCH:
            {
                std::cout << "hello world . i am murus\n" ;
                auto* itch_ptr = std::get_if<ITCHPayload>(&event.payload);
                if (!itch_ptr) break;

                // if(auto* sod = std::get_if<StartofDay>(itch_ptr)) {     
                //   optional_react(); // i have to define this function . for hft . initally my plan is two three hft interacting with each other.
                // }
                
                // LOB* lob_ptr = nullptr;
                // MarketState m(); // will default runs or what . why parameterized dont cause error ?
                // switch (event.symbol) {
                //     case Symbol::AAPL: lob_ptr = LOB_appl; m = M_State_appl; break;
                //     case Symbol::MSFT: lob_ptr = LOB_msft; m = M_State_msft; break;
                //     case Symbol::TSLA: lob_ptr = LOB_tsla; m = M_State_tsla; break;
                //     case Symbol::SPY:  lob_ptr = LOB_spy;  m = M_State_spy;  break;
                //     default: break;
                // }
                // if (!lob_ptr) break;

                // update_market_state(*lob_ptr ,event , m);
            

                break;
            }

       
            case EventType::OUCH:
            {

                LOB* lob_ptr = nullptr;
                switch (event.symbol) {
                    case Symbol::AAPL: lob_ptr = LOB_appl; break;
                    case Symbol::MSFT: lob_ptr = LOB_msft; break;
                    case Symbol::TSLA: lob_ptr = LOB_tsla; break;
                    case Symbol::SPY:  lob_ptr = LOB_spy;  break;
                    default: break;
                }
                if (!lob_ptr) break;

                engine->process_ouch_request(event, *lob_ptr,
                                            feed_hq, seq_number);
                break;
            }

            case EventType::SPECIFIC_OUCH:
            {
                AgentTier tier  = event.agent_tier;
                uint32_t  index = event.agent_index;

                
                auto* sp = std::get_if<SpecificOUCHPayload>(&event.payload);
                if (sp) {
                    if (auto* fill = std::get_if<FillNotification>(sp)) {
                        update_agent_on_fill(tier, index, *fill);
                    }
                    else if (auto* cancel = std::get_if<CancelAccepted>(sp)) {
                        update_agent_on_cancel(tier, index, *cancel);
                    }
                    else if (auto* resting = std::get_if<OrderRestingNotification>(sp)) {
                        update_agent_on_resting(tier, index, *resting);
                    }
                    else if(auto * p = std::get_if<ReplaceAccepted>(sp)){
                        update_agent_on_replace(tier , index , *p);
                    }
                    // OrderRejected / CancelRejected: no state change,
                    // just give agent a chance to react (retry logic etc)
                }

                optional_react_specific(tier, index, event,
                                        reaction_queue, available_order_id);

            
                if (!reaction_queue.empty()) {
                    uint64_t l1 = get_agent_l1(tier, index);
                    uint64_t l2 = get_agent_l2(tier, index);
                    uint64_t reaction_time = event.timestamp + l1 + l2;

                    for (auto& request : reaction_queue) {
                        request.timestamp    = reaction_time;
                        request.sequence_num = seq_number++;
                        
                        Global_SQ->push(request);
                    }
                    reaction_queue.clear();
                }

                break;
            }

        } // end switch

        // --------------------------------------------------------
        // BATCH SCHEDULE: Mode 2 reactions into Global_SQ
        // Timestamps set by agents using eq(p):
        //   ouch.timestamp = itch.timestamp + L1 + L2
        // Kernel assigns sequence numbers here before pushing.
        // --------------------------------------------------------
        if (!reaction_queue.empty()) {
            for (auto& r : reaction_queue) {
                r.sequence_num = seq_number++;
                Global_SQ->push(r);
            }
        }

        // --------------------------------------------------------
        // BATCH SCHEDULE: Mode 1 engine output into Global_SQ
        // CHANGED: was iterating reaction_queue by mistake (bug
        // in old kernel). Now correctly iterates feed_hq.
        // Events in feed_hq already have timestamps from engine
        // (LOB clock) and seq_nums from seq_number counter.
        // --------------------------------------------------------
        if (!feed_hq.empty()) {
            for (auto& f : feed_hq) {
                Global_SQ->push(f);
            }
        }

    } // end while


    std::cout << "Simulation queue is empty. Exiting cleanly.\n";

    // 4. PREVENT MEMORY LEAKS
    delete Global_SQ; 
    delete engine; 
    delete LOB_appl; 
    delete LOB_msft; 
    delete LOB_tsla; 
    delete LOB_spy;


    return 0;
}
