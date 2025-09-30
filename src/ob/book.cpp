#include "book.hpp"
#include <algorithm>
#include <utility>

OrderBook::OrderBook(std::string symbol, int64_t tick)
    : symbol_(std::move(symbol)), tick_{tick} {}

bool OrderBook::add(const Order& o) {
    // qty must be positive
    if (o.qty <= 0) return false;

    // Reject duplicate IDs (replace() will own updates later)
    if (id_index_.find(o.id) != id_index_.end()) return false;

    const bool is_market = (o.type == Type::Market);

    // LIMIT-specific validations (MARKET has no price/tick check)
    if (!is_market) {
        if (o.px <= 0 || (o.px % tick_) != 0) return false; // tick check
    }

    // Local lambdas so this function is standalone.
    auto would_cross_limit = [&](const Order& x) -> bool {
        if (x.type == Type::Market) return true;
        if (x.side == Side::Buy) {
            if (ask_levels_.empty()) return false;
            const int64_t best_ask = ask_levels_.begin()->first; // lowest ask
            return x.px >= best_ask;
        } else {
            if (bid_levels_.empty()) return false;
            const int64_t best_bid = bid_levels_.begin()->first; // highest bid (map uses greater<>)
            return x.px <= best_bid;
        }
    };

    auto can_fully_fill = [&](const Order& x) -> bool {
        int64_t need = x.qty;
        if (need <= 0) return true;

        if (x.side == Side::Buy) {
            for (auto it = ask_levels_.begin(); it != ask_levels_.end() && need > 0; ++it) {
                const int64_t px = it->first;
                if (x.type == Type::Limit && px > x.px) break; // don’t cross past limit
                const auto& dq = it->second.q;
                for (const auto& row : dq) {
                    need -= row.qty;
                    if (need <= 0) return true;
                }
            }
        } else {
            for (auto it = bid_levels_.begin(); it != bid_levels_.end() && need > 0; ++it) {
                const int64_t px = it->first;
                if (x.type == Type::Limit && px < x.px) break; // don’t cross past limit
                const auto& dq = it->second.q;
                for (const auto& row : dq) {
                    need -= row.qty;
                    if (need <= 0) return true;
                }
            }
        }
        return need <= 0;
    };

    // POST-ONLY: reject if it would cross (or if market)
    if (o.tif == TIF::PostOnly) {
        if (is_market || would_cross_limit(o)) return false;

        // rest without matching
        if (o.side == Side::Buy) {
            auto [it, _] = bid_levels_.try_emplace(o.px, Level{o.px});
            it->second.enqueue(o.id, o.qty, o.ts_ns);
            id_index_[o.id] = Handle{Side::Buy, o.px};
        } else {
            auto [it, _] = ask_levels_.try_emplace(o.px, Level{o.px});
            it->second.enqueue(o.id, o.qty, o.ts_ns);
            id_index_[o.id] = Handle{Side::Sell, o.px};
        }
        return true;
    }

    // FOK: must be fully fillable upfront; if not, reject
    if (o.tif == TIF::FOK) {
        if (!can_fully_fill(o)) return false;
        Order in = o;
        match_incoming(in);          // consume everything
        return in.qty == 0;          // by design should be fully filled
    }

    // MARKET: cross as much as possible and never rest
    if (is_market) {
        Order in = o;
        match_incoming(in);
        return true;                  // markets never rest (v1)
    }

    // LIMIT (DAY or IOC): match first; then maybe rest
    Order in = o;
    match_incoming(in);

    // IOC: do not rest residual
    if (o.tif == TIF::IOC) return true;

    // Rest any remainder FIFO at its price level (DAY or similar)
    if (in.qty > 0) {
        if (in.side == Side::Buy) {
            auto [it, _] = bid_levels_.try_emplace(in.px, Level{in.px});
            it->second.enqueue(in.id, in.qty, in.ts_ns);
            id_index_[in.id] = Handle{Side::Buy, in.px};
        } else {
            auto [it, _] = ask_levels_.try_emplace(in.px, Level{in.px});
            it->second.enqueue(in.id, in.qty, in.ts_ns);
            id_index_[in.id] = Handle{Side::Sell, in.px};
        }
    }
    return true;
}

bool OrderBook::cancel(uint64_t id, int64_t /*ts*/) {
    auto it_idx = id_index_.find(id);
    if (it_idx == id_index_.end()) return false;

    const Handle h = it_idx->second;
    bool removed = false;

    auto erase_from_level = [&](auto& level_map) {
        auto it_lvl = level_map.find(h.px);
        if (it_lvl == level_map.end()) return false;

        auto& dq = it_lvl->second.q;

        // Linear scan
        for (auto it = dq.begin(); it != dq.end(); ++it) {
            if (it->id == id) {
                dq.erase(it);
                // If the level is empty, remove the price level entirely.
                if (dq.empty()) level_map.erase(it_lvl);
                return true;
            }
        }
        return false;
    };

    if (h.side == Side::Buy) {
        removed = erase_from_level(bid_levels_);
    } else {
        removed = erase_from_level(ask_levels_);
    }

    if (removed) id_index_.erase(it_idx);
    return removed;
}

bool OrderBook::replace(uint64_t id, int64_t new_px, int64_t new_qty, int64_t ts_ns) {
    // sanity
    if (new_qty <= 0) return false;

    // locate by id
    auto it_idx = id_index_.find(id);
    if (it_idx == id_index_.end()) return false;

    const Handle h = it_idx->second;

    // ---------- branch by side to avoid ternary type mismatch ----------
    if (h.side == Side::Buy) {
        auto it_lvl = bid_levels_.find(h.px);
        if (it_lvl == bid_levels_.end()) return false;

        auto& dq = it_lvl->second.q;
        auto it_row = std::find_if(dq.begin(), dq.end(), [&](const auto& r){ return r.id == id; });
        if (it_row == dq.end()) return false;

        const bool price_change = (new_px != h.px);
        if (price_change) {
            // tick check for price changes
            if (new_px <= 0 || (new_px % tick_) != 0) return false;

            // remove from current level
            dq.erase(it_row);
            if (dq.empty()) bid_levels_.erase(it_lvl);
            id_index_.erase(it_idx);

            // treat as fresh incoming LIMIT (may trade immediately)
            Order in;
            in.id    = id;
            in.side  = Side::Buy;
            in.type  = Type::Limit;
            in.tif   = TIF::Day;   // simple default for replace
            in.px    = new_px;
            in.qty   = new_qty;
            in.ts_ns = ts_ns;

            match_incoming(in);

            if (in.qty > 0) {
                auto [new_it, _] = bid_levels_.try_emplace(new_px, Level{new_px});
                new_it->second.enqueue(id, in.qty, ts_ns);
                id_index_[id] = Handle{Side::Buy, new_px};
            }
            return true;
        } else {
            // price unchanged
            if (new_qty == it_row->qty) return true; // nothing to do

            if (new_qty < it_row->qty) {
                // shrink in place: keep FIFO position
                it_row->qty = new_qty;
                return true;
            } else {
                // increase: reset time (move to back)
                auto row = *it_row;
                row.qty   = new_qty;
                row.ts_ns = ts_ns;
                dq.erase(it_row);
                dq.push_back(row);
                return true;
            }
        }

    } else { // ---------- SELL side ----------
        auto it_lvl = ask_levels_.find(h.px);
        if (it_lvl == ask_levels_.end()) return false;

        auto& dq = it_lvl->second.q;
        auto it_row = std::find_if(dq.begin(), dq.end(), [&](const auto& r){ return r.id == id; });
        if (it_row == dq.end()) return false;

        const bool price_change = (new_px != h.px);
        if (price_change) {
            if (new_px <= 0 || (new_px % tick_) != 0) return false;

            dq.erase(it_row);
            if (dq.empty()) ask_levels_.erase(it_lvl);
            id_index_.erase(it_idx);

            Order in;
            in.id    = id;
            in.side  = Side::Sell;
            in.type  = Type::Limit;
            in.tif   = TIF::Day;
            in.px    = new_px;
            in.qty   = new_qty;
            in.ts_ns = ts_ns;

            match_incoming(in);

            if (in.qty > 0) {
                auto [new_it, _] = ask_levels_.try_emplace(new_px, Level{new_px});
                new_it->second.enqueue(id, in.qty, ts_ns);
                id_index_[id] = Handle{Side::Sell, new_px};
            }
            return true;
        } else {
            if (new_qty == it_row->qty) return true;

            if (new_qty < it_row->qty) {
                it_row->qty = new_qty;   // shrink, keep place
                return true;
            } else {
                auto row = *it_row;
                row.qty   = new_qty;
                row.ts_ns = ts_ns;
                dq.erase(it_row);
                dq.push_back(row);        // increase, move to back
                return true;
            }
        }
    }
}

bool OrderBook::match_incoming(Order& in) {
    bool any = false;
    const bool is_market = (in.type == Type::Market);

    if (in.side == Side::Buy) {
        // Cross against ASKS
        auto& opp = ask_levels_;
        while (in.qty > 0 && !opp.empty()) {
            auto it_lvl = opp.begin();              // best ask (lowest px)
            const int64_t trade_px = it_lvl->first;
            if (!is_market && in.px < trade_px) break;

            auto& dq = it_lvl->second.q;            // FIFO at this price
            while (in.qty > 0 && !dq.empty()) {
                auto& maker = dq.front();
                const int64_t exec = std::min(in.qty, maker.qty);

                // --- record trade at resting price (ASK level) ---
                trades_.push_back(Trade{
                    /*taker_id=*/in.id,
                    /*maker_id=*/maker.id,
                    /*px=*/trade_px,
                    /*qty=*/exec,
                    /*ts_ns=*/in.ts_ns,          // good enough for v1
                    /*taker_is_buy=*/true
                });

                // apply fill
                in.qty    -= exec;
                maker.qty -= exec;
                any = true;

                if (maker.qty == 0) {
                    id_index_.erase(maker.id);
                    dq.pop_front();
                } else {
                    // partial at front; taker might be done
                    break;
                }
            }
            if (dq.empty()) opp.erase(it_lvl);
        }
    } else {
        // Cross against BIDS (best bid at begin() due to greater<>)
        auto& opp = bid_levels_;
        while (in.qty > 0 && !opp.empty()) {
            auto it_lvl = opp.begin();              // best bid (highest px)
            const int64_t trade_px = it_lvl->first;
            if (!is_market && in.px > trade_px) break;

            auto& dq = it_lvl->second.q;            // FIFO at this price
            while (in.qty > 0 && !dq.empty()) {
                auto& maker = dq.front();
                const int64_t exec = std::min(in.qty, maker.qty);

                // --- record trade at resting price (BID level) ---
                trades_.push_back(Trade{
                    /*taker_id=*/in.id,
                    /*maker_id=*/maker.id,
                    /*px=*/trade_px,
                    /*qty=*/exec,
                    /*ts_ns=*/in.ts_ns,
                    /*taker_is_buy=*/false
                });

                // apply fill
                in.qty    -= exec;
                maker.qty -= exec;
                any = true;

                if (maker.qty == 0) {
                    id_index_.erase(maker.id);
                    dq.pop_front();
                } else {
                    break;
                }
            }
            if (dq.empty()) opp.erase(it_lvl);
        }
    }

    return any;
}


bool OrderBook::would_cross_limit(const Order& in) const
{
    if(in.type == Type::Market) return true;
    if(in.side == Side::Buy)
    {
        if(ask_levels_.empty()) return false;
        return in.px >= ask_levels_.begin()->first;
    } else 
    {
        if(bid_levels_.empty()) return false;
        return in.px <= bid_levels_.begin()->first;
    }
}

bool OrderBook::can_fully_fill(const Order& in) const
{
    int64_t need = in.qty;
    if(need <= 0) return true;

    if(in.side == Side::Buy)
    {
        for (auto it = ask_levels_.begin(); it != ask_levels_.end() && need > 0; ++it)
        {
            const int64_t px = it->first;
            if(in.type == Type::Limit && px > in.px) break;
            const auto& dq = it->second.q;
            for(const auto& row : dq) { need -= row.qty; if (need <= 0) return true;}
        }
    } else 
    {
        for(auto it = bid_levels_.begin(); it != bid_levels_.end() && need > 0; ++it)
        {
            const int64_t px = it->first;
            if(in.type == Type::Limit && px < in.px) break;
            const auto& dq = it->second.q;
            for(const auto& row : dq) {need -= row.qty; if (need <= 0) return true;}
        }
    }
    return need <= 0;
}

std::vector<LevelView> OrderBook::bids(int depth) const
{
    std::vector<LevelView> out;
    out.reserve(depth);
    int n = 0;
    for (auto it = bid_levels_.begin(); it != bid_levels_.end() && n < depth; ++it, ++n) {
        const auto& lvl = it->second;
        out.push_back(LevelView{lvl.px, lvl.total_qty(), lvl.count()});
    }
    return out;
}

std::vector<LevelView> OrderBook::asks(int depth) const
{
    std::vector<LevelView> out;
    out.reserve(depth);
    int n = 0;
    for (auto it = ask_levels_.begin(); it != ask_levels_.end() && n < depth; ++it, ++n) {
        const auto& lvl = it->second;
        out.push_back(LevelView{lvl.px, lvl.total_qty(), lvl.count()});
    }
    return out;
}
