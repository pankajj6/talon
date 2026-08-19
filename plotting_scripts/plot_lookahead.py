import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('Agg') # Forces background rendering
import matplotlib.pyplot as plt

# ---- Theme: dark companions of the LaTeX softblue/softpink/softgray
COLOR_TIME = "#2F5C8A"   # dark steel blue (softblue family)
COLOR_PRICE = "#A8355C"  # dark rose       (softpink family)
COLOR_GRID = "#5A5F66"   # dark gray       (softgray family)

print("Loading CSV data in background...")
df = pd.read_csv('lookahead_leakage_log.csv')

# Determine ANY state mismatch (including Empty vs. Populated)
df['is_leakage'] = (df['shadow_bid'] != df['exchange_bid']) | (df['shadow_ask'] != df['exchange_ask'])

# Calculate safe Mid-Prices ONLY when both books are fully populated
valid_books = (
    (df['shadow_bid'] > 0.0) & 
    (df['shadow_ask'] > 0.0) & 
    (df['exchange_bid'] > 0.0) & 
    (df['exchange_ask'] > 0.0)
)

df.loc[valid_books, 'safe_shadow_mid'] = (df['shadow_bid'] + df['shadow_ask']) / 2.0
df.loc[valid_books, 'safe_exchange_mid'] = (df['exchange_bid'] + df['exchange_ask']) / 2.0
df.loc[valid_books, 'clean_price_diff'] = df['safe_exchange_mid'] - df['safe_shadow_mid']

# 4. Calculate the headline metrics
total_triggers = len(df)
leakage_triggers = df['is_leakage'].sum()
leakage_pct = (leakage_triggers / total_triggers) * 100 if total_triggers > 0 else 0

print(f"=== LOOK-AHEAD BIAS EVALUATION ===")
print(f"Total Reactive Agent Triggers: {total_triggers:,}")
print(f"Triggers with State Leakage (Any Mismatch): {leakage_triggers:,} ({leakage_pct:.2f}%)")

# Temporal Analysis
avg_time_jump_ns = df['time_diff_ns'].mean()
max_time_jump_ns = df['time_diff_ns'].max()
print(f"Average Exchange LOB Clock Jump Ahead: {avg_time_jump_ns:,.2f} ns")
print(f"Max Exchange LOB Clock Jump Ahead: {max_time_jump_ns:,.2f} ns")

# Plotting Histograms
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

# Plot A: Temporal Look-Ahead (in microseconds)
ax1.hist(df['time_diff_ns'] / 1000.0, bins=50, color=COLOR_TIME, alpha=0.85, edgecolor='black', linewidth=0.8)
ax1.set_title('Temporal Look-Ahead Gap\n(LOB Clock - ITCH Event Time)', fontsize=13, fontweight='bold')
ax1.set_xlabel('Time Difference (Microseconds μs)', fontsize=11)
ax1.set_ylabel('Frequency', fontsize=11)
ax1.grid(True, linestyle="--", color=COLOR_GRID, alpha=0.3)

# Plot B: Price Discrepancy (Only for non-zero, valid mid-price differences)
non_zero_price_diffs = df['clean_price_diff'].dropna()
non_zero_price_diffs = non_zero_price_diffs[non_zero_price_diffs != 0]

if len(non_zero_price_diffs) > 0:
    ax2.hist(non_zero_price_diffs, bins=50, color=COLOR_PRICE, alpha=0.85, edgecolor='black', linewidth=0.8)
    ax2.set_title('Mid-Price Discrepancy\n(Exchange Mid - Shadow Mid)', fontsize=13, fontweight='bold')
    ax2.set_xlabel('Price Discrepancy ($ USD or ticks)', fontsize=11)
    ax2.set_ylabel('Frequency', fontsize=11)
    ax2.grid(True, linestyle="--", color=COLOR_GRID, alpha=0.3)

plt.tight_layout()
plt.savefig('lookahead_dual_eval1.png', dpi=300)
print("SUCCESS: Image generated safely and saved as 'lookahead_dual_eval1.png'")

