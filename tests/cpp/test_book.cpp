#include "ob/book.hpp"
#include "ob/order.hpp"
#include <gtest/gtest.h>

TEST(Book, InsertAndSnapshot) {
    OrderBook ob("TEST", 1);

    Order bid{1, Side::Buy,  Type::Limit, TIF::Day, 10000, 50,  1, false};
    Order ask{2, Side::Sell, Type::Limit, TIF::Day, 10100, 30,  2, false};

    ASSERT_TRUE(ob.add(bid));
    ASSERT_TRUE(ob.add(ask));

    auto bs = ob.bids(5);
    auto as = ob.asks(5);

    ASSERT_FALSE(bs.empty());
    ASSERT_FALSE(as.empty());

    EXPECT_EQ(bs[0].px,  10000);
    EXPECT_EQ(bs[0].qty, 50);
    EXPECT_EQ(bs[0].orders, 1u);

    EXPECT_EQ(as[0].px,  10100);
    EXPECT_EQ(as[0].qty, 30);
    EXPECT_EQ(as[0].orders, 1u);
}
TEST(Book, CancelById) {
    OrderBook ob("TEST", 1);

    // Two bids at the same price; cancel the first by ID.
    ASSERT_TRUE(ob.add(Order{10, Side::Buy, Type::Limit, TIF::Day, 10000, 40,  1, false}));
    ASSERT_TRUE(ob.add(Order{11, Side::Buy, Type::Limit, TIF::Day, 10000, 20,  2, false}));
    ASSERT_TRUE(ob.add(Order{20, Side::Sell, Type::Limit, TIF::Day, 10100, 50,  3, false}));

    auto bs1 = ob.bids(5);
    ASSERT_EQ(bs1[0].px, 10000);
    EXPECT_EQ(bs1[0].qty, 60);
    EXPECT_EQ(bs1[0].orders, 2u);

    // Cancel id=10
    EXPECT_TRUE(ob.cancel(10, /*ts*/4));

    auto bs2 = ob.bids(5);
    ASSERT_EQ(bs2[0].px, 10000);
    EXPECT_EQ(bs2[0].qty, 20);
    EXPECT_EQ(bs2[0].orders, 1u);

    // Cancel non-existent
    EXPECT_FALSE(ob.cancel(999, 5));
}

TEST(Book, MarketableLimit_SweepsBestPrices_FIFOResting) {
    OrderBook ob("TEST", 1);

    // Resting asks: 10100x30 (older), 10100x10 (newer), 10150x20
    ASSERT_TRUE(ob.add(Order{1, Side::Sell, Type::Limit, TIF::Day, 10100, 30, 1, false}));
    ASSERT_TRUE(ob.add(Order{2, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 2, false}));
    ASSERT_TRUE(ob.add(Order{3, Side::Sell, Type::Limit, TIF::Day, 10150, 20, 3, false}));

    // Incoming BUY limit @10150 for 35:
    // Fills 30 (id=1) then 5 from (id=2) — FIFO at 10100 — remainder posts? none (0 left)
    ASSERT_TRUE(ob.add(Order{9, Side::Buy, Type::Limit, TIF::Day, 10150, 35, 4, false}));

    // Snapshot: 10100 now has 5 left (from id=2), and 10150 still 20
    auto as = ob.asks(5);
    ASSERT_EQ(as.size(), 2u);
    EXPECT_EQ(as[0].px, 10100);
    EXPECT_EQ(as[0].qty, 5);
    EXPECT_EQ(as[0].orders, 1u);
    EXPECT_EQ(as[1].px, 10150);
    EXPECT_EQ(as[1].qty, 20);
    EXPECT_EQ(as[1].orders, 1u);

    // Bids remain empty (incoming fully consumed)
    auto bs = ob.bids(5);
    EXPECT_TRUE(bs.empty());
}

TEST(Book, MarketOrder_ConsumesOppositeAndDoesNotRest) {
    OrderBook ob("TEST", 1);

    // Resting asks: 10050x15, 10075x20
    ASSERT_TRUE(ob.add(Order{10, Side::Sell, Type::Limit, TIF::Day, 10050, 15, 1, false}));
    ASSERT_TRUE(ob.add(Order{11, Side::Sell, Type::Limit, TIF::Day, 10075, 20, 2, false}));

    // Incoming BUY MARKET for 25: takes 15@10050 + 10@10075; leaves 10075x10
    ASSERT_TRUE(ob.add(Order{12, Side::Buy, Type::Market, TIF::IOC, 0, 25, 3, false}));

    auto as = ob.asks(5);
    ASSERT_EQ(as.size(), 1u);
    EXPECT_EQ(as[0].px, 10075);
    EXPECT_EQ(as[0].qty, 10);
    EXPECT_EQ(as[0].orders, 1u);

    auto bs = ob.bids(5);
    EXPECT_TRUE(bs.empty());
}
TEST(Book, LimitIOC_DoesNotRestResidual) {
    OrderBook ob("TEST", 1);
    ASSERT_TRUE(ob.add(Order{1, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 1, false}));
    ASSERT_TRUE(ob.add(Order{2, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 2, false}));

    // IOC buy @10100 for 25 -> fills 20, residual 5 discarded, nothing rests
    ASSERT_TRUE(ob.add(Order{3, Side::Buy,  Type::Limit, TIF::IOC, 10100, 25, 3, false}));

    auto as = ob.asks(5);
    ASSERT_EQ(as.size(), 0u); // both asks consumed

    auto bs = ob.bids(5);
    EXPECT_TRUE(bs.empty());  // IOC residual must not rest
}

TEST(Book, FOK_RejectsIfNotFullyFillable) {
    OrderBook ob("TEST", 1);
    ASSERT_TRUE(ob.add(Order{1, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 1, false}));

    // Needs 15 but only 10 available at/below 10100 -> reject, book unchanged
    EXPECT_FALSE(ob.add(Order{2, Side::Buy, Type::Limit, TIF::FOK, 10100, 15, 2, false}));

    auto as = ob.asks(5);
    ASSERT_EQ(as.size(), 1u);
    EXPECT_EQ(as[0].px, 10100);
    EXPECT_EQ(as[0].qty, 10);

    auto bs = ob.bids(5);
    EXPECT_TRUE(bs.empty());
}

TEST(Book, PostOnly_RejectsIfWouldCross_OtherwiseRests) {
    OrderBook ob("TEST", 1);
    ASSERT_TRUE(ob.add(Order{1, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 1, false}));

    // Crossing -> reject
    EXPECT_FALSE(ob.add(Order{2, Side::Buy,  Type::Limit, TIF::PostOnly, 10100, 5, 2, false}));

    // Not crossing -> rest
    ASSERT_TRUE(ob.add(Order{3, Side::Buy,  Type::Limit, TIF::PostOnly, 10050, 7, 3, false}));

    auto bs = ob.bids(5);
    ASSERT_EQ(bs.size(), 1u);
    EXPECT_EQ(bs[0].px, 10050);
    EXPECT_EQ(bs[0].qty, 7);
}

TEST(Book, Trades_RecordPartialAndMultiLevel)
{
    OrderBook ob("Test", 1);
    ASSERT_TRUE(ob.add(Order{1, Side::Sell, Type::Limit, TIF::Day, 10100, 30, 1, false}));
    ASSERT_TRUE(ob.add(Order{2, Side::Sell, Type::Limit, TIF::Day, 10150, 20, 1, false}));

    //Buy 40 at 10150 -> 30 at 10100 + 10 at 10150

    ASSERT_TRUE(ob.add(Order{3, Side::Buy, Type::Limit, TIF::Day, 10150, 40, 3, false}));

    auto trades = ob.pop_trade();

    ASSERT_EQ(trades.size(), 2u);
    
    EXPECT_EQ(trades[0].taker_id, 3u);
    EXPECT_EQ(trades[0].maker_id, 1u);
    EXPECT_EQ(trades[0].px, 10100);
    EXPECT_EQ(trades[0].qty, 30);
    EXPECT_TRUE(trades[0].taker_is_buy);

    EXPECT_EQ(trades[1].taker_id, 3u);
    EXPECT_EQ(trades[1].maker_id, 2u);
    EXPECT_EQ(trades[1].px, 10150);
    EXPECT_EQ(trades[1].qty, 10);
    EXPECT_TRUE(trades[1].taker_is_buy);
}

TEST(Book, MarketTrades_DoNotRest_AndAreRecorded) {
    OrderBook ob("TEST", 1);
    ASSERT_TRUE(ob.add(Order{10, Side::Sell, Type::Limit, TIF::Day, 10050, 15, 1, false}));
    ASSERT_TRUE(ob.add(Order{11, Side::Sell, Type::Limit, TIF::Day, 10075, 20, 2, false}));

    ASSERT_TRUE(ob.add(Order{12, Side::Buy,  Type::Market, TIF::IOC, 0, 25, 3, false}));
    auto trades = ob.pop_trade();
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].px, 10050);
    EXPECT_EQ(trades[0].qty, 15);
    EXPECT_EQ(trades[1].px, 10075);
    EXPECT_EQ(trades[1].qty, 10);

    auto bs = ob.bids(5);
    EXPECT_TRUE(bs.empty());
}

TEST(Book, Replace_ShrinkKeepsPlace_IncreaseResetsTime) {
    OrderBook ob("TEST", 1);

    // Two sells at same price; id=1 is older (ahead), id=2 is newer (behind)
    ASSERT_TRUE(ob.add(Order{1, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 1, false}));
    ASSERT_TRUE(ob.add(Order{2, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 2, false}));

    // Shrink id=1 from 10 -> 6: should keep its place (still ahead of id=2)
    ASSERT_TRUE(ob.replace(1, 10100, 6, /*ts*/3));

    // Marketable buy 8 @ >=10100: should fill 6 from id=1, then 2 from id=2
    ASSERT_TRUE(ob.add(Order{9, Side::Buy, Type::Limit, TIF::Day, 10150, 8, 4, false}));

    auto trades = ob.pop_trade();
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].maker_id, 1u);
    EXPECT_EQ(trades[0].qty, 6);
    EXPECT_EQ(trades[1].maker_id, 2u);
    EXPECT_EQ(trades[1].qty, 2);

    // Reset: add back two at same price
    OrderBook ob2("TEST", 1);
    ASSERT_TRUE(ob2.add(Order{1, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 1, false}));
    ASSERT_TRUE(ob2.add(Order{2, Side::Sell, Type::Limit, TIF::Day, 10100, 10, 2, false}));

    // Increase id=1 to 12 -> should move to BACK (after id=2)
    ASSERT_TRUE(ob2.replace(1, 10100, 12, /*ts*/3));

    // Buy 10 at 10150: should hit id=2 first (since id=1 reset), then id=1
    ASSERT_TRUE(ob2.add(Order{9, Side::Buy, Type::Limit, TIF::Day, 10150, 15, 4, false}));
    auto t2 = ob2.pop_trade();    
    ASSERT_EQ(t2.size(), 2u);
    EXPECT_EQ(t2[0].maker_id, 2u);
    EXPECT_EQ(t2[0].qty, 10);  // all of id=2
    EXPECT_EQ(t2[1].maker_id, 1u);
    EXPECT_EQ(t2[1].qty, 5);   // remainder from id=1
}

TEST(Book, Replace_PriceChangeCanTrade_OrRepost) {
    OrderBook ob("TEST", 1);
    // Resting asks far
    ASSERT_TRUE(ob.add(Order{10, Side::Sell, Type::Limit, TIF::Day, 10200, 10, 1, false}));
    ASSERT_TRUE(ob.add(Order{11, Side::Sell, Type::Limit, TIF::Day, 10300, 10, 1, false}));
    // Rest a BUY at 10050
    ASSERT_TRUE(ob.add(Order{1, Side::Buy,  Type::Limit, TIF::Day, 10050, 12, 2, false}));

    // Replace price up to 10200 with qty 12 -> should immediately trade 10@10200, then rest 2@10200
    ASSERT_TRUE(ob.replace(1, 10200, 12, /*ts*/3));

    auto trades = ob.pop_trade();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].taker_id, 1u);
    EXPECT_EQ(trades[0].px, 10200);
    EXPECT_EQ(trades[0].qty, 10);

    auto bs = ob.bids(5);
    ASSERT_EQ(bs.size(), 1u);
    EXPECT_EQ(bs[0].px, 10200);
    EXPECT_EQ(bs[0].qty, 2);
}