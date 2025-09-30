# Low-Latency Order Book Simulator (C++20 + pybind11)

Single-instrument with price-time priority, matching for Market and Limit orders, TIF, (IOC / FOK/ PostOnly), cancel replaceand minimal trade event buffer

Current State: Core enginge + unit tests

Features 
  - Matching engine
      1) FIFO within each price level, trades executes at the resting price
      2) Market orders + aggressive Limit orders (multi-level sweeps, partial fills)
      3) TIF: IOC, FOK, PostOnly

  - Order maintenance
      1)cancel(id) by order ID
      2)replace(id, new_px, new_qty) rules:
                a)Same price: shrink to keep palce, increase: move to back
                b) New Price: loses priority (re-enters;may trade immediately if aggressive)

- Snapshots & events
    1)bids(depth) / asks(depth) (L2 summaries)
    2)pop_trade() returns executed trades since the last call

  -Tests
      1)Matching, IOC, FOK, Postonly, Cancel, replace, and trade logging


  Future work: (Ordered from current work -> last item)
    - Add examples.cpp with simple demo
    - replace() and pop_trades() in pybind11
    - Engine enhancements, O(1) cancel.replace
    - multi-thread saftey
    - metrics.py
    - plots.py
    - Benchmakrs and Profiling
    - CSV/SQLite writer
