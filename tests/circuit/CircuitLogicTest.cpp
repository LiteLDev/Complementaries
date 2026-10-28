#include <cstdlib>

#include <gtest/gtest.h>

#include "feature/circuit/CircuitLogic.h"

namespace c = cm::feature::circuit;

TEST(CircuitLogic, Directions) {
    for (int f = 0; f < 6; ++f) {
        auto o = c::facingOffset(f);
        auto b = c::facingOffset(c::oppositeFacing(f));
        EXPECT_EQ(o.x + b.x, 0);
        EXPECT_EQ(o.y + b.y, 0);
        EXPECT_EQ(o.z + b.z, 0);
        EXPECT_EQ(std::abs(o.x) + std::abs(o.y) + std::abs(o.z), 1);
    }
    for (int f = 2; f < 6; ++f) EXPECT_EQ(c::cardinalToFacing(c::facingToCardinal(f)), f);
    // a display facing south is looked at from the south (viewer looks north): right hand = east
    EXPECT_EQ(c::viewerRight(3), 5);
    EXPECT_EQ(c::viewerRight(2), 4);
    EXPECT_EQ(c::viewerLeft(3), 4);
}

TEST(CircuitLogic, ChainRoles) {
    EXPECT_EQ(c::chainRole(0, 1), c::Chain::Alone);
    EXPECT_EQ(c::chainRole(0, 2), c::Chain::Tens);
    EXPECT_EQ(c::chainRole(1, 2), c::Chain::Units);
    EXPECT_EQ(c::chainRole(2, 3), c::Chain::Alone);
    EXPECT_EQ(c::chainRole(2, 4), c::Chain::Tens);
    EXPECT_EQ(c::chainRole(3, 4), c::Chain::Units);
    EXPECT_EQ(c::pairValue(4, 2), 42);
    EXPECT_EQ(c::pairValue(15, 15), 99);
}

TEST(CircuitLogic, Digits) {
    auto lone = c::displayDigits(7, c::Chain::Alone);
    EXPECT_EQ(lone.left, -1);
    EXPECT_EQ(lone.right, 7);
    auto teen = c::displayDigits(13, c::Chain::Alone);
    EXPECT_EQ(teen.left, 1);
    EXPECT_EQ(teen.right, 3);
    EXPECT_EQ(c::displayDigits(12, c::Chain::Units).big, 9);
    EXPECT_EQ(c::displayDigits(0, c::Chain::Tens).big, 0);
    EXPECT_EQ(c::kSegments[8], 0x7F);
    EXPECT_EQ(c::kSegments[1], 0x06);
}

TEST(CircuitLogic, TruncateUtf8) {
    EXPECT_EQ(c::truncateUtf8("hello", 3), "hel");
    EXPECT_EQ(c::truncateUtf8("hi", 10), "hi");
    EXPECT_EQ(c::truncateUtf8("\xE4\xBD\xA0\xE5\xA5\xBD!", 1), "\xE4\xBD\xA0"); // one CJK character
    EXPECT_EQ(c::truncateUtf8("\xE4\xBD\xA0\xE5\xA5\xBD!", 2), "\xE4\xBD\xA0\xE5\xA5\xBD");
    EXPECT_EQ(c::truncateUtf8("\xE4\xBD", 5), ""); // a cut-off sequence is dropped
    EXPECT_EQ(c::truncateUtf8("abc", 0), "");
}

TEST(CircuitLogic, Clamps) {
    EXPECT_EQ(c::illuminatorLight(20), 15);
    EXPECT_EQ(c::illuminatorLight(-1), 0);
    EXPECT_EQ(c::clampVolume(0, 64), 1);
    EXPECT_EQ(c::clampVolume(100, 64), 64);
}
