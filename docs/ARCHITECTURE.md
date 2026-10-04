# Architecture Diagram

## System Evolution

```mermaid
graph TD
    A[V0 Reference Order Book<br/>std::map + std::deque] --> B[V1 Cache-Friendly Order Book<br/>std::vector + intrusive list]
    B --> C[V1 + Object Pool<br/>Chunk-based freelist]
    C --> D[Multi-Instrument Engine<br/>Orchestration layer]
    D --> E{Concurrency Model}
    E -->|Mutex Baseline| F[MutexMultiInstrumentEngine<br/>Global std::mutex]
    E -->|SPSC Ingestion| G[SpscMultiInstrumentEngine<br/>Lock-free SPSC queue]
    
    style A fill:#f9f,stroke:#333,stroke-width:2px
    style B fill:#bbf,stroke:#333,stroke-width:2px
    style C fill:#bfb,stroke:#333,stroke-width:2px
    style D fill:#fbf,stroke:#333,stroke-width:2px
    style F fill:#fbb,stroke:#333,stroke-width:2px
    style G fill:#bfb,stroke:#333,stroke-width:2px
```

## Multi-Instrument Architecture

```mermaid
graph TD
    A[MultiInstrumentEngine] --> B[Instrument A]
    A --> C[Instrument B]
    A --> D[Instrument C]
    
    B --> E[OrderBookV1Pool]
    C --> F[OrderBookV1Pool]
    D --> G[OrderBookV1Pool]
    
    E --> H[Bids: sorted vector]
    E --> I[Asks: sorted vector]
    E --> J[Order index: unordered_map]
    E --> K[Object pool: freelist]
    
    F --> L[Bids: sorted vector]
    F --> M[Asks: sorted vector]
    F --> N[Order index: unordered_map]
    F --> O[Object pool: freelist]
    
    style A fill:#fbf,stroke:#333,stroke-width:2px
    style E fill:#bfb,stroke:#333,stroke-width:2px
    style F fill:#bfb,stroke:#333,stroke-width:2px
```

## Mutex Concurrency Baseline

```mermaid
graph TD
    A[Producer Thread 1] --> B[global mutex]
    C[Producer Thread 2] --> B
    D[Producer Thread N] --> B
    
    B --> E[MutexMultiInstrumentEngine<br/>Wrapper]
    
    E --> F[MultiInstrumentEngine<br/>Single-threaded]
    
    F --> G[Instrument A -> OrderBookV1Pool]
    F --> H[Instrument B -> OrderBookV1Pool]
    F --> I[Instrument C -> OrderBookV1Pool]
    
    style B fill:#fbb,stroke:#333,stroke-width:3px
    style E fill:#fbf,stroke:#333,stroke-width:2px
    style F fill:#bbf,stroke:#333,stroke-width:2px
```

## SPSC Ingestion Architecture

```mermaid
graph TD
    A[Producer Thread] -->|enqueue| B[SPSC Ring Buffer<br/>Fixed capacity<br/>Power-of-two]
    
    B -->|dequeue| C[Consumer Thread<br/>Sole writer of order-book state]
    
    C --> D[SpscMultiInstrumentEngine]
    
    D --> E[MultiInstrumentEngine<br/>Owned exclusively by consumer]
    
    E --> F[Instrument A -> OrderBookV1Pool]
    E --> G[Instrument B -> OrderBookV1Pool]
    E --> H[Instrument C -> OrderBookV1Pool]
    
    style B fill:#bfb,stroke:#333,stroke-width:3px
    style C fill:#fbf,stroke:#333,stroke-width:3px
    style D fill:#bbf,stroke:#333,stroke-width:2px
    style E fill:#fbf,stroke:#333,stroke-width:2px
```

## SPSC Queue Memory Ordering

```mermaid
sequenceDiagram
    participant P as Producer
    participant Q as SPSC Queue
    participant C as Consumer
    
    P->>Q: load(read_index, acquire)
    Note over P,Q: See consumer progress
    P->>Q: new (&buffer[current_write]) T(value)
    Note over P,Q: Construct value
    P->>Q: store(write_index, release)
    Note over P,Q: Publish value
    
    Q->>C: load(write_index, acquire)
    Note over Q,C: See published data
    C->>Q: value = move(buffer[current_read])
    Note over Q,C: Read value
    C->>Q: buffer[current_read].~T()
    Note over Q,C: Destroy old value
    C->>Q: store(read_index, release)
    Note over Q,C: Announce progress
```

## Single-Writer Invariant

The SPSC architecture enforces a critical invariant:

**ONLY the matching-engine consumer thread directly mutates order-book state.**

This is enforced by:
- API design: `SpscMultiInstrumentEngine` only exposes enqueue methods
- No direct access: Underlying `MultiInstrumentEngine` is private (except test-only)
- Clear documentation: Ownership boundary is explicit

```mermaid
graph LR
    A[Producer Thread] -->|Can only enqueue commands| B[SPSC Queue]
    B -->|Commands flow to| C[Consumer Thread]
    C -->|Sole writer| D[Order Book State]
    
    style A fill:#bbf,stroke:#333,stroke-width:2px
    style C fill:#bfb,stroke:#333,stroke-width:3px
    style D fill:#fbb,stroke:#333,stroke-width:3px
```

## Order Book Data Structure Evolution

### V0 (Baseline)

```mermaid
graph TD
    A[Price Level] --> B[std::map&lt;Price, std::deque&lt;Order&gt;&gt;]
    B --> C[OrderNode in deque]
    C --> D[Order]
    
    E[Order Index] --> F[std::unordered_map&lt;OrderId, OrderLocation&gt;]
    F --> G[Location: side, price, deque_index]
    
    style B fill:#f9f,stroke:#333,stroke-width:2px
    style F fill:#f9f,stroke:#333,stroke-width:2px
```

### V1 (Cache-Friendly)

```mermaid
graph TD
    A[Price Level] --> B[std::vector&lt;PriceLevel&gt;<br/>Sorted, contiguous]
    B --> C[Intrusive Doubly-Linked List]
    C --> D[OrderNode: order, prev, next, price, side]
    
    E[Order Index] --> F[std::unordered_map&lt;OrderId, OrderNode*&gt;<br/>Direct pointer]
    
    style B fill:#bbf,stroke:#333,stroke-width:2px
    style F fill:#bbf,stroke:#333,stroke-width:2px
```

### V1 + Object Pool

```mermaid
graph TD
    A[Price Level] --> B[std::vector&lt;PriceLevel&gt;<br/>Sorted, contiguous]
    B --> C[Intrusive Doubly-Linked List]
    C --> D[OrderNode: order, prev, next, price, side]
    
    E[Order Index] --> F[std::unordered_map&lt;OrderId, OrderNode*&gt;<br/>Direct pointer]
    
    G[Object Pool] --> H[Chunk-based freelist]
    H --> I[Pool hits: 99.99%]
    H --> J[Chunk growth: 1K → 2K → 4K → 8K → 16K → 32K → 64K]
    
    style B fill:#bfb,stroke:#333,stroke-width:2px
    style F fill:#bfb,stroke:#333,stroke-width:2px
    style H fill:#bfb,stroke:#333,stroke-width:2px
```
