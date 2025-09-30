#pragma once
#include <cstdint>
#include <deque>


struct QueueEntry 
{
    uint64_t id;
    int64_t qty;
    int64_t ts_ns;
};

//One price level in book (FIFO queue of orders at that price)

struct Level 
{
    int64_t px{0};
    std::deque<QueueEntry> q;

    Level() = default;
    explicit Level(int64_t p): px(p) {}

    void enqueue(uint64_t id, int64_t qty, int64_t ts_ns)
    {
        q.push_back({id, qty, ts_ns});
    }
    
    int64_t total_qty() const
    {
        int64_t tot = 0;
        for (const auto& e : q) tot += e.qty;
        return tot;
    }

    size_t count() const { return q.size(); }
};