import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap

# ---- Theme: dark companions of the LaTeX softblue/softgreen/softorange/softgray/softpink
#      fills, matching liquidity_depth_analysis2.png / lob_market_state_clean.png ----
COLOR_BID    = "#2E8B57"   # dark seagreen   (softgreen family)
COLOR_ASK    = "#A8355C"   # dark rose       (softpink family)
COLOR_MID    = "#2F5C8A"   # dark steel blue (softblue family)
COLOR_GRID   = "#5A5F66"   # dark gray       (softgray family)

# Diverging colormap built from the same ask/bid hues instead of stock RdYlGn
theme_diverging = LinearSegmentedColormap.from_list(
    "theme_diverging", [COLOR_ASK, "#FAFAF7", COLOR_BID]
)

# 1. Load and Downsample Data for Fast Rendering
df_state = pd.read_csv("sim_output.csv")
df_depth = pd.read_csv("lob_depth.csv")

target_cols = 2500
if len(df_depth) > target_cols:
    step = len(df_depth) // target_cols
    df_depth = df_depth.iloc[::step].reset_index(drop=True)
    df_state = df_state.iloc[::step].reset_index(drop=True)

FACTOR = 10000.0  # Scale 10,000 integer units to $1.00 USD
time_sec = (df_depth["timestamp"] - df_depth["timestamp"].iloc[0]) / 1e9

# --- DYNAMIC WARMUP SLICE ---
warmup_cutoff = len(df_depth) // 10
df_depth = df_depth.iloc[warmup_cutoff:].reset_index(drop=True)
df_state = df_state.iloc[warmup_cutoff:].reset_index(drop=True)
time_sec = time_sec.iloc[warmup_cutoff:].reset_index(drop=True)

target_cols = 2500
if len(df_depth) > target_cols:
    step = len(df_depth) // target_cols

# 2. Define Dollar Price Grid
price_cols = [c for c in df_depth.columns if "_p" in c]
valid_prices = df_depth[price_cols].values.flatten()
valid_prices = valid_prices[valid_prices > 0] / FACTOR

min_price = np.percentile(valid_prices, 0.5)
max_price = np.percentile(valid_prices, 99.5)
tick_size_usd = 100.0 / FACTOR  # 100 integer units = $0.01 (1 cent)

price_grid = np.arange(min_price, max_price + tick_size_usd, tick_size_usd)
num_bins = len(price_grid) - 1
num_steps = len(df_depth)

# 3. Vectorized 2D Grid Population
volume_matrix = np.zeros((num_bins, num_steps))

for i in range(1, 11):
    # Process Bids
    bp = df_depth[f"bid_p{i}"].values / FACTOR
    bv = df_depth[f"bid_v{i}"].values
    valid_bids = (bp >= min_price) & (bp < max_price) & (bv > 0)
    bid_idx = ((bp[valid_bids] - min_price) // tick_size_usd).astype(int)
    bid_idx = np.clip(bid_idx, 0, num_bins - 1)
    volume_matrix[bid_idx, np.where(valid_bids)[0]] += bv[valid_bids]

    # Process Asks
    ap = df_depth[f"ask_p{i}"].values / FACTOR
    av = df_depth[f"ask_v{i}"].values
    valid_asks = (ap >= min_price) & (ap < max_price) & (av > 0)
    ask_idx = ((ap[valid_asks] - min_price) // tick_size_usd).astype(int)
    ask_idx = np.clip(ask_idx, 0, num_bins - 1)
    volume_matrix[ask_idx, np.where(valid_asks)[0]] -= av[valid_asks]

# 4. Render Plot in Real USD
plt.figure(figsize=(16, 9))

non_zero_vols = np.abs(volume_matrix[volume_matrix != 0])
max_vol = np.percentile(non_zero_vols, 92) if len(non_zero_vols) > 0 else 100

T, P = np.meshgrid(time_sec, price_grid[:-1])

plt.pcolormesh(T, P, volume_matrix, cmap=theme_diverging, vmin=-max_vol, vmax=max_vol, shading='auto')
plt.colorbar(label="Volume (dark rose = Ask Shares | dark green = Bid Shares)")

# Overlay L1 Top-of-Book trajectories in Dollars
best_bid = (df_state["best_bid"].replace(0, np.nan)) / FACTOR
best_ask = (df_state["best_ask"].replace(0, np.nan)) / FACTOR
mid_price = (df_state["mid_price"].replace(0, np.nan)) / FACTOR

plt.plot(time_sec, best_bid, color=COLOR_BID, linewidth=1.5, label="Best Bid ($)", zorder=4)
plt.plot(time_sec, best_ask, color=COLOR_ASK, linewidth=1.5, label="Best Ask ($)", zorder=4)
plt.plot(time_sec, mid_price, color=COLOR_MID, linestyle="--", linewidth=1.2, label="Mid Price ($)", zorder=5)

plt.title("LOB Price Trajectory & Book Depth Dynamics (USD Scale)", fontsize=16, fontweight="bold", pad=15)
plt.xlabel("Simulation Time (Seconds)", fontsize=12)
plt.ylabel("Price ($ USD)", fontsize=12)
plt.ylim(min_price, max_price)
plt.legend(loc="upper left", framealpha=0.9)
plt.grid(True, linestyle=":", alpha=0.4, color=COLOR_GRID)

plt.tight_layout()
plt.savefig("price_trajectory_human.png", dpi=300)
print("Saved themed dollar heatmap to price_trajectory_human.png")