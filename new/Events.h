#pragma once
// ============================================================
// Events.h
// All event types, payload variants, and enums.
//
// Three event categories:
//   OUCH          — agent request to exchange (L2)
//                   Kernel MODE 1: pop OUCH, call matching engine
//   ITCH          — broadcast exchange to ALL agents (L1)
//                   Kernel MODE 2: pop ITCH, iterate all agents
//   SPECIFIC_OUCH — private exchange to ONE agent (L3)
//                   Kernel MODE 3: update one agent, let them react
//
// L3 < L1 always.
//
// CHANGED from previous version:
//   - ReplaceAccepted added to SpecificOUCHPayload
//   - ReplaceRejected added to SpecificOUCHPayload
//   - CancelRejected gains reason field (was just order_id)
//     reason 0 = order already filled/not in system
// ============================================================

#include <variant>
#include <cstdint>

// ============================================================
// ENUMS
// ============================================================

enum class EventType : uint8_t {
    OUCH          = 0,   // MODE 1: agent request to exchange
    ITCH          = 1,   // MODE 2: broadcast to all agents
    SPECIFIC_OUCH = 2    // MODE 3: private exchange-to-one-agent
};

enum class AgentTier : uint8_t {
    HFT           = 0,
    MARKET_MAKER  = 1,
    INSTITUTIONAL = 2,
    RETAIL        = 3,
    EXCHANGE      = 4
};

enum class Symbol : uint8_t {
    AAPL = 0,
    MSFT = 1,
    TSLA = 2,
    SPY  = 3
};

// ADDED: used by Order struct and engine for buy/sell logic
enum class Order_Side : uint8_t {
    Buy  = 0,
    Sell = 1
};

// ADDED: rejection reason codes used by engine
enum class Reason : uint8_t {
    duplicate_order_id = 0,
    invalid_price      = 1,
    invalid_quantity   = 2,
    book_empty         = 3,
    orderId_NOT_Found  = 4
};

// ============================================================
// OUCH PAYLOAD TYPES
// Agent requests to exchange via L2 latency.
// ============================================================

struct EnterLimitOrder {
    uint64_t order_id;
    uint64_t price;
    int32_t  quantity;
    uint8_t  side;          // 0=buy, 1=sell
    uint8_t  time_in_force; // 0=DAY, 1=IOC, 2=GTC
};

struct EnterMarketOrder {
    uint64_t order_id;
    int32_t  quantity;
    uint8_t  side;
};

struct CancelOrder {
    uint64_t order_id;
    int32_t  max_quantity;  // 0=full cancel, >0=reduce to this size
};

struct ReplaceOrder {
    uint64_t old_order_id;
    uint64_t new_order_id;
    uint64_t new_price;
    int32_t  new_quantity;
};

using OUCHPayload = std::variant<
    EnterLimitOrder,
    EnterMarketOrder,
    CancelOrder,
    ReplaceOrder
>;

// ============================================================
// ITCH PAYLOAD TYPES
// Public broadcast to ALL agents via L1 latency.
// ============================================================

struct OrderAdded {
    uint64_t order_id;
    uint64_t price;
    int32_t  quantity;
    uint8_t  side;
};

struct OrderExecuted {
    uint64_t order_id;
    uint64_t price;        // ADDED: needed by market_state to determine bid/ask vol
    int32_t  executed_qty;
    uint64_t match_number;
};

struct OrderCancelled {
    uint64_t order_id;
    int32_t  cancelled_qty;
    uint8_t  side;         // ADDED: needed by market_state liquidity update
};

struct OrderReplaced {
    uint64_t old_order_id;
    uint64_t new_order_id;
    uint64_t new_price;
    int32_t  new_quantity;
    int32_t  old_quantity; // ADDED: for volume tracking in market_state
    uint8_t  side;         // ADDED: for market_state liquidity update
};

// ADDED: synthetic ITCH types used by kernel internally
struct StartofDay {};       // seeds the simulation loop
struct FairPriceUpdate {};  // triggers fair price model update + fundamentalist reaction

using ITCHPayload = std::variant<
    OrderAdded,
    OrderExecuted,
    OrderCancelled,
    OrderReplaced,
    StartofDay,      // ADDED
    FairPriceUpdate  // ADDED
>;

// ============================================================
// SPECIFIC_OUCH PAYLOAD TYPES
// Private exchange-to-one-agent via L3 latency.
// Kernel MODE 3: update agent state, then let them react.
// ============================================================

// One per fill event — sent to BOTH passive and aggressive agent.
struct FillNotification {
    uint64_t order_id;
    int32_t  filled_qty;
    uint64_t price;
    uint8_t  side;
    int32_t  remaining_qty;  // 0=fully filled
};

// Cancel request succeeded.
// Public OrderCancelled ITCH also generated after this.
struct CancelAccepted {
    uint64_t order_id;
    int32_t  qty_remaining;  // 0=fully cancelled
};

// CHANGED: reason field added.
// reason 0 = order already filled or not in system (cannot cancel).
struct CancelRejected {
    uint64_t order_id;
    uint8_t  reason;  // CHANGED: added. 0 = not found / already filled
};

// Enter request rejected privately. No public ITCH.
struct OrderRejected {
    uint64_t order_id;
    Reason   reason;  // CHANGED: typed enum instead of uint8_t
};

// Limit order now resting in book (passive or aggressive-then-passive).
// Paired with public OrderAdded ITCH pushed after this.
struct OrderRestingNotification {
    uint64_t order_id;
    uint64_t price;
    int32_t  resting_qty;
    uint8_t  side;
};

// CHANGED: new. Replace succeeded.
// Sent to agent: old order cancelled, new order resting.
// Paired with public OrderReplaced ITCH pushed after this.
struct ReplaceAccepted {
    uint64_t new_order_id;
    uint64_t new_price;
    int32_t  old_cancelled_qty;  // remaining qty of old order that was cancelled
    int32_t  new_resting_qty;    // qty of new resting order
    uint8_t  side;
};

// CHANGED: new. Replace rejected privately. No public ITCH.
// reason: 0 = old order already filled / not in system
struct ReplaceRejected {
    uint64_t old_order_id;  // CHANGED: was new_order_id in your sketch — old makes more sense
    uint8_t  reason;
};

using SpecificOUCHPayload = std::variant<
    FillNotification,
    CancelAccepted,
    CancelRejected,
    OrderRejected,
    OrderRestingNotification,
    ReplaceAccepted,    // CHANGED: added
    ReplaceRejected     // CHANGED: added
>;

// ============================================================
// UNIFIED PAYLOAD
// ============================================================
using Payload = std::variant<
    OUCHPayload,
    ITCHPayload,
    SpecificOUCHPayload
>;

// ============================================================
// EVENT STRUCT
// event_type tells kernel which mode:
//   OUCH          -> MODE 1 (matching engine)
//   ITCH          -> MODE 2 (iterate all agents)
//   SPECIFIC_OUCH -> MODE 3 (update one agent, let them react)
// ============================================================
struct Event {
    uint64_t   timestamp       = 0;
    uint64_t   sequence_num    = 0;
    uint64_t   causal_parent_id = 0;
    EventType  event_type      = EventType::ITCH;
    Symbol     symbol          = Symbol::AAPL;
    AgentTier  agent_tier      = AgentTier::EXCHANGE;
    uint32_t   agent_index     = 0;
    Payload    payload;
};
