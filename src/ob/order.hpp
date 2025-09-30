#pragma once
#include <cstdint>


enum class Side{Buy, Sell};
enum class Type{Limit, Market};
enum class TIF{Day, IOC, FOK, GTC, PostOnly};


struct Order 
{
    uint64_t id{};
    Side side{};
    Type type{};
    TIF tif{TIF::Day};
    int64_t px{};
    int64_t qty{};
    int64_t ts_ns{};
    bool post_only{false};
};

struct Trade 
{
    uint64_t taker_id;
    uint64_t maker_id;
    int64_t px;
    int64_t qty;
    int64_t ts_ns;
    bool taker_is_buy;
};