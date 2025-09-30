#pragma once

#include <map>
#include <unordered_map>
#include "order.hpp"
#include "price_level.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>


struct LevelView{int64_t px; int64_t qty; size_t orders;};


class OrderBook 
{
    public:
        OrderBook(std::string symbol, int64_t tick);

        bool add(const Order& o);
        bool cancel(uint64_t id, int64_t ts);
        bool replace(uint64_t id, int64_t new_px, int64_t new_qty, int64_t ts);

        std::vector<LevelView> bids(int depth) const;
        std::vector<LevelView> asks(int depth) const;

        std::vector<Trade> pop_trade() 
        {
            auto out = std::move(trades_);
            trades_.clear();
            return out;
        }

    private:
        std::string symbol_;
        int64_t tick_{1};

        //price ladder

        std::map<int64_t, Level, std::greater<int64_t>> bid_levels_;
        std::map<int64_t, Level> ask_levels_;

        struct Handle {Side side; int64_t px;};
        std::unordered_map<uint64_t, Handle> id_index_;
        
        std::vector<Trade> trades_;
        //Some helpers
        bool match_incoming(Order& in);
        bool can_fully_fill(const Order& in) const;
        bool would_cross_limit(const Order& in) const;
};
