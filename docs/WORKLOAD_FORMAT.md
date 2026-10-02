# Workload Format and CLI Tools

## Overview

This document describes the deterministic order flow generator, replay harness, and CLI tools for the matching engine.

## Workload File Format

Workload files are text-based files containing a sequence of orders to be executed against the matching engine. The format is designed to be human-readable and reproducible.

### File Structure

A workload file consists of three sections:

1. **Configuration Header** - Metadata about the generation parameters
2. **Format Description** - Comments explaining the data format
3. **Order Data** - One line per order

### Example

```
# Workload Configuration
num_orders=100
limit_ratio=0.9
buy_ratio=0.5
mid_price=100000
spread=100
volatility=0.01
cancel_rate=0.1
rng_seed=42
min_qty=1
max_qty=100
# End Configuration
# Format: sequence|action|side|price|quantity|order_id
# Actions: 0=SubmitLimit, 1=SubmitMarket, 2=Cancel
# Sides: 0=Buy, 1=Sell
1|0|0|99890|45|1
2|0|1|100110|23|2
3|0|0|99750|67|3
...
```

### Field Descriptions

- **sequence**: Monotonically increasing order number (1-indexed)
- **action**: Order action type
  - `0` = SubmitLimit
  - `1` = SubmitMarket
  - `2` = Cancel
- **side**: Order side
  - `0` = Buy
  - `1` = Sell
- **price**: Order price in cents (0 for market orders)
- **quantity**: Order quantity
- **order_id**: Unique order identifier (used for cancel operations)

### Configuration Parameters

- **num_orders**: Total number of orders generated
- **limit_ratio**: Ratio of limit orders (0.0 to 1.0)
- **buy_ratio**: Ratio of buy orders (0.0 to 1.0)
- **mid_price**: Mid price around which orders are distributed (in cents)
- **spread**: Spread around mid price (in cents, half-spread)
- **volatility**: Price volatility as ratio of mid price (e.g., 0.01 = 1%)
- **cancel_rate**: Probability of cancel vs submit (0.0 to 1.0)
- **rng_seed**: RNG seed for reproducibility
- **min_qty**: Minimum order quantity
- **max_qty**: Maximum order quantity

## order_gen CLI Tool

The `order_gen` tool generates deterministic order flow workloads.

### Usage

```bash
order_gen [OPTIONS]
```

### Options

- `-o, --output FILE` - Output workload file (default: workload.txt)
- `-R, --replay FILE` - Output replay file (versioned, self-contained)
- `-n, --num-orders N` - Number of orders to generate (default: 1000)
- `-l, --limit-ratio R` - Ratio of limit orders 0.0-1.0 (default: 0.9)
- `-b, --buy-ratio R` - Ratio of buy orders 0.0-1.0 (default: 0.5)
- `-m, --mid-price P` - Mid price in cents (default: 100000)
- `-s, --spread S` - Spread in cents (default: 100)
- `-v, --volatility V` - Price volatility ratio (default: 0.01)
- `-c, --cancel-rate R` - Cancel probability 0.0-1.0 (default: 0.1)
- `-r, --rng-seed SEED` - RNG seed for reproducibility (default: 42)
- `-q, --min-qty Q` - Minimum order quantity (default: 1)
- `-Q, --max-qty Q` - Maximum order quantity (default: 100)
- `-h, --help` - Show help message

### Examples

Generate a small workload:
```bash
order_gen -o small.txt -n 100
```

Generate a small replay file:
```bash
order_gen -R small.replay -n 100
```

Generate a large deterministic workload:
```bash
order_gen -o large.txt -n 1000000 -r 12345
```

Generate a large replay file:
```bash
order_gen -R large.replay -n 1000000 -r 12345
```

Generate with custom parameters:
```bash
order_gen -o custom.txt -l 0.8 -b 0.6 -v 0.02 -m 50000
```

## engine_cli CLI Tool

The `engine_cli` tool executes a workload or replay file against the matching engine and reports statistics.

### Usage

```bash
engine_cli [OPTIONS] WORKLOAD_FILE
```

### Options

- `-r, --replay` - Execute as replay file (with version checking)
- `-s, --stats FILE` - Write statistics to file (optional)
- `-h, --help` - Show help message

### Examples

Execute a workload and print statistics:
```bash
engine_cli workload.txt
```

Execute a replay file:
```bash
engine_cli -r replay.txt
```

Execute and save statistics:
```bash
engine_cli -s results.txt workload.txt
```

Execute replay and save statistics:
```bash
engine_cli -r -s results.txt replay.txt
```

### Output Statistics

The tool reports the following statistics:

- **Orders processed**: Total number of orders executed
- **Trades**: Number of trades generated
- **Traded quantity**: Total quantity traded across all trades
- **Final book depth**: Number of resting orders on each side
- **Final book levels**: Number of price levels on each side
- **Execution time**: Time taken to execute the workload (ms)
- **Throughput**: Orders processed per second

### Example Output

```
Executing 100 orders...
Execution completed.

=== Execution Statistics ===
Orders processed: 100
Trades: 13
Traded quantity: 410
Final book depth:
  Bid orders: 34
  Ask orders: 36
Final book levels:
  Bid levels: 20
  Ask levels: 13
Execution time: 14.069 ms
Throughput: 7107.83 orders/sec
============================
```

## Determinism and Reproducibility

The workload generator is fully deterministic when using the same RNG seed. This means:

1. Running `order_gen` with the same parameters produces identical output files
2. The same workload file always produces identical execution results
3. Performance comparisons are meaningful across runs

To verify determinism:
```bash
# Generate twice with same seed
order_gen -o test1.txt -n 1000 -r 42
order_gen -o test2.txt -n 1000 -r 42

# Compare files (should be identical)
diff test1.txt test2.txt
```

## Generator Design

### Order Generation Algorithm

The generator uses the following process:

1. **Action Selection**: For each order, randomly choose between submit and cancel based on `cancel_rate`. Cancels are only issued if active orders exist.

2. **Order Type Selection**: For submit orders, choose between limit and market based on `limit_ratio`.

3. **Side Selection**: Choose buy or sell based on `buy_ratio`.

4. **Price Generation**:
   - Uses normal distribution with mean = `mid_price` and std dev = `mid_price * volatility`
   - For buy orders: clamped to ≤ `mid_price - spread`
   - For sell orders: clamped to ≥ `mid_price + spread`
   - Market orders have price = 0

5. **Quantity Generation**: Uniform random between `min_qty` and `max_qty`.

6. **Order ID Assignment**: Sequential monotonically increasing IDs.

7. **Cancel Selection**: Randomly selects from active order IDs for cancellation.

### RNG Implementation

The generator uses `std::mt19937_64` (Mersenne Twister) for deterministic pseudo-random number generation. The seed is user-configurable to ensure reproducibility.

## Performance Characteristics

### Small Workload (100 orders)
- Debug mode: ~14ms execution time
- Release mode: ~0.08ms execution time
- Throughput: ~1.2M orders/sec (Release)

### Large Workload (1M orders)
- Debug mode: ~864s execution time
- Release mode: ~1993s execution time
- Throughput: ~500 orders/sec (Release)

Note: The Release mode performance for 1M orders appears slower than Debug due to the large book state (600K+ resting orders) and the overhead of maintaining the order index. This is expected for the unoptimized v0 engine.

## Limitations

1. **No Order Modification**: The generator does not support order modification (only submit and cancel).
2. **Single Instrument**: Workloads are for a single order book (no multi-instrument support).
3. **No Time-Based Features**: Orders are not timestamped; execution is sequential.
4. **Simple Price Distribution**: Uses normal distribution; more sophisticated market microstructure modeling is not implemented.
5. **Cancel Logic**: Cancels are randomly selected from active orders without considering order age or priority.

## Replay File Format

Replay files are versioned, self-contained files that store the exact order stream needed to reproduce a workload deterministically. Unlike workload files (which include generation configuration), replay files contain only the order data required for execution.

### File Structure

A replay file consists of three sections:

1. **Version Header** - Format version identifier and metadata
2. **Format Description** - Comments explaining the data format
3. **Order Data** - One line per order

### Example

```
# Replay File - Self-contained order stream for deterministic replay
replay_version=1
num_orders=100
# End Header
# Format: sequence|action|side|price|quantity|order_id
# Actions: 0=SubmitLimit, 1=SubmitMarket, 2=Cancel
# Sides: 0=Buy, 1=Sell
1|0|0|99890|45|1
2|0|1|100110|23|2
3|0|0|99750|67|3
...
```

### Field Descriptions

The order data fields are identical to the workload format:

- **sequence**: Monotonically increasing order number (1-indexed)
- **action**: Order action type
  - `0` = SubmitLimit
  - `1` = SubmitMarket
  - `2` = Cancel
- **side**: Order side
  - `0` = Buy
  - `1` = Sell
- **price**: Order price in cents (0 for market orders)
- **quantity**: Order quantity
- **order_id**: Unique order identifier (used for cancel operations)

### Versioning

Replay files include a `replay_version` field in the header to support format evolution:

- **Current version**: 1
- **Purpose**: Allows future format changes while maintaining backward compatibility
- **Validation**: The `engine_cli` tool checks the version before execution and rejects unsupported versions

### Self-Contained Nature

Replay files are self-contained and do not require:
- Regeneration from RNG seed
- Original generator configuration
- Access to the generation tool

All information needed to reproduce the workload is stored directly in the file.

### Generating Replay Files

Use the `-R` or `--replay` option with `order_gen`:

```bash
order_gen -R replay.txt -n 1000 -r 42
```

### Executing Replay Files

Use the `-r` or `--replay` option with `engine_cli`:

```bash
engine_cli -r replay.txt
engine_cli -r -s results.txt replay.txt
```

### Determinism Guarantees

Replay files provide strong determinism guarantees:

1. **Same file, same results**: Executing the same replay file multiple times produces identical:
   - Trade count
   - Traded quantity
   - Final book state (depth and levels)
   - Event sequence

2. **No external dependencies**: Replay files contain all required data; no RNG or configuration needed

3. **Version validation**: Replay execution fails if the format version is unsupported

### Differences from Workload Files

| Feature | Workload File | Replay File |
|---------|---------------|-------------|
| Purpose | Store generation config + orders | Store orders for replay |
| Self-contained | No (includes generation params) | Yes (orders only) |
| Versioned | No | Yes |
| Format evolution | Not intended | Supported via version field |
| Use case | Configuration storage | Deterministic replay |

### Replay File Format Evolution

When the replay format needs to change:

1. Increment `REPLAY_FORMAT_VERSION` in `order_generator.hpp`
2. Update the parser in `read_replay_file()` to handle both old and new versions
3. Document the changes in this file
4. Existing replay files with older versions remain supported

### Testing Replay Determinism

To verify replay determinism:

```bash
# Generate a replay file
order_gen -R test.replay -n 1000 -r 42

# Execute twice and compare results
engine_cli -r -s run1.txt test.replay
engine_cli -r -s run2.txt test.replay

# Compare statistics (should be identical)
diff run1.txt run2.txt
```

Or use the automated replay tests:

```bash
# Run replay-specific tests
ctest -R replay
```

