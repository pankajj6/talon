## ⚠️ CRITICAL LICENSE & INTELLECTUAL PROPERTY NOTICE
As of June 2026, this entire repository, its C++ core simulator, and all attached theoretical derivation assets have been permanently transitioned to the **GNU Affero General Public License v3.0 (AGPL-3.0)**. 

Any ongoing use, modification, server deployment, or algorithmic replication of these systems or their 100+ pages of mathematical derivations legally mandates that your entire software infrastructure must be made fully open-source under the same strict AGPL terms. Independent academic publication or commercial reuse without explicit co-authorship and legal compliance is strictly prohibited.

# USim

A C++ Discrete Event Simulation (DES) designed to model financial market microstructure with physical-layer accuracy. By enforcing strict information boundaries and deterministic sequencing, USim prevents the temporal paradoxes ("future information leaks") common in standard high-frequency modeling.

## Architecture & Infrastructure
* **Zero-Allocation Matching:** Core loops utilize a pre-allocated `storage_pool` with a stack-based free-index tracker to eliminate heap fragmentation and `malloc` overhead during high-burst events.
* **Custom Array-Based LOB:** Uses a contiguous array-based architecture instead of standard library maps or linked lists. This ensures O(log N) price discovery with O(1) memory locality, maximizing L1 cache hits.
* **Cache-Targeted DOD:** Built with Data-Oriented Design. While it runs efficiently on standard hardware (e.g., a 20MB cache laptop), deploying this on a CPU with a massive L3 cache allows the engine to keep the order book and agent pools almost entirely on-chip, resulting in blistering execution speeds.
* **Deterministic Sequencing:** Every event is assigned a `seq_num` to break ties in same-nanosecond arrivals, ensuring the simulation remains perfectly reproducible.
* **Architecture Design:** The research/architecture/ directory contains the original first-principle derivations. These notes document the core logic of the simulation universe—from solving the "Same Timestamp" collision problem to modeling physical-layer latency boundaries (L1/L2/L3),etc.

## The Kernel: 3-Mode Orchestrator
The Kernel manages a global Custom Priority Queue (Min-Heap), sequencing every event by nanosecond-precision timestamps. It operates as a three-mode state machine:
1. **Mode 1 (OUCH - Exchange Processing):** Pops agent requests and feeds them to the Matching Engine. It captures the resulting `feed_hq` (ITCH/Specific OUCH) and schedules them into the global timeline based on the Engine's internal clock and accumulated Processing Time (PT).
2. **Mode 2 (ITCH - Market Broadcast):** Upon popping a public ITCH message, the Kernel updates the `MarketState` (Shadow LOB). It then polls broad agent reactions (HFT, Retail, Institutional) based on their specific L1 (Broadcast) and L2 (Thinking) latencies.
3. **Mode 3 (SPECIFIC_OUCH - Private Lifecycle):** Handles private messages (Fills, Cancels, Rests, Rejects) sent to individual ports. This updates an agent's internal Inventory and Cash balances and allows for a "Specific React" cycle, ensuring the agent knows its own state before the public world does.

## Temporal Integrity
Standard simulators often "cheat" by allowing agents to instantly query the Engine's current state, accidentally providing them with "Future Information."
* **The Solution:** The Kernel maintains a separate `MarketState`. It reconstructs the order book message-by-message using the ITCH feed. When an agent reacts to ITCH #1, the Shadow LOB reflects the book exactly as it was at that microsecond, even if the actual Matching Engine has already moved ahead to ITCH #100.

## Tiered Information
* **Institutional/HFT:** Consume raw ITCH data and the highly-granular reconstructed `MarketState`.
* **Retail:** Rely on a filtered `BrokerSnapshot` featuring high-level indicators like `mid_price` trends and `spread`, simulating real-world platform delays.

## Current R&D: Solving "Ghost Liquidity"
Initial benchmarks processed 1.8 Million events in 20 seconds. These tests revealed that naive HFT agents act as "Ghost Liquidity," cancelling orders too aggressively in response to ITCH noise.
* **Pivot to Alpha:** We are currently implementing Avellaneda-Stoikov market-making models to anchor agents with Inventory Risk and Fair Price processes (Ornstein-Uhlenbeck). This ensures agents provide stable liquidity based on position skew rather than simple, erratic price-matching.

## Technical Components
* `infra/src/engine.cpp`: Core matching logic (Limit/Market orders, Cancel/Update).
* `infra/include/market_state.h`: Shadow LOB reconstruction and volume analytics.
* `kernel/main.cpp`: The central DES loop and agent pool coordination.
* `infra/src/custom_priority_queue.cpp`: Custom min-heap for nanosecond event sequencing.

## License
This project is licensed strictly under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. 

See the [LICENSE](LICENSE) file for the full legal text. Any downstream use, modification, server deployment, or algorithmic replication of this simulation architecture requires full compliance with copyleft terms, proper author attribution, and open-sourcing of any derived frameworks.
