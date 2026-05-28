#pragma once
#include <cstdint>
#include "events.h"
#include "market_state.h"
#include <utility>
#include <unordered_map>
#include "order.h"


extern uint64_t available_order_id;

// ============================================================
// HELPER: build a bare OUCH event shell (caller fills payload)
// ============================================================
inline Event make_ouch(uint64_t timestamp, Symbol sym, AgentTier tier, uint32_t idx, uint64_t causal)
{
    Event e;
    e.event_type        = EventType::OUCH;
    e.timestamp         = timestamp;
    e.symbol            = sym;
    e.agent_tier        = tier;
    e.agent_index       = idx;
    e.causal_parent_id  = causal;
    return e;
}


struct activeorder{
    uint64_t price;
    int qty; // negative or positive
};

using ordr_map = std::unordered_map<uint64_t, activeorder> ; 

struct Market_maker{
    uint32_t l1;
    uint32_t l2;
    int q = 0; // inventory
    uint64_t Efair_price ;
    int error;
    int16_t y;// risk aversion // gamma
    // uint64_t r; //reservation price.

    ordr_map buy_ordrs ;
    ordr_map sell_ordrs ;
    uint32_t total_orders ;


};


void cancl_ordr(){}

void send_order(){


}


void Market_making(Event& event , Market_maker& mm , MarketState& m_s){

    auto tick = Config::TICK_SIZE ;
    auto s = m_s.mid_price ; // mid price.
    uint64_t r ; // reservation price.
    auto sig = m_s.volatility;

    uint8_t remtime; // remaining time . normalized. scale of 1
    uint64_t buyquote;
    uint64_t sellquote;



    if(mm.Efair_price == m_s.mid_price  && mm.total_orders < 10 )
    {
        if((m_s.spread/2) >= Config::TICK_SIZE){
            buyquote = m_s.mid_price - (m_s.spread/2) ;
            sellquote = m_s.mid_price + (m_s.spread/2);
            send_order();
            mm.total_orders += 2;
        }
        else{
            buyquote = m_s.mid_price - (m_s.spread/2) ;
            sellquote = m_s.mid_price + (m_s.spread/2);
            buyquote -= (buyquote%tick);
            sellquote -= (sellquote%tick);
            send_order();
            mm.total_orders += 2;
        }
    }


    // if()
    // {}







}

