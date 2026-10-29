// Unit tests of the jar unit's pure soft-fluid logic (src/feature/jar/{Fluids,Contents}.cpp have
// no game headers, so they are compiled into the test binary directly).

#include <gtest/gtest.h>

#include "feature/jar/Contents.cpp"
#include "feature/jar/Fluids.cpp"

using namespace cm::feature::jar;

TEST(JarFluids, BucketsAndBottles) {
    Tank t{{}, 12};
    auto water = fluidFromItem({"minecraft:water_bucket", 0});
    ASSERT_TRUE(water);
    EXPECT_EQ(water->fluid.count, kBucket);
    EXPECT_EQ(water->emptyId, "minecraft:bucket");
    EXPECT_EQ(t.add(water->fluid, false), 4);
    EXPECT_EQ(t.add(water->fluid, false), 4);
    EXPECT_EQ(t.add(water->fluid, false), 4);
    EXPECT_EQ(t.add(water->fluid, true), 0); // full at 12
    auto bottle = fillItem(t.fluid, "minecraft:glass_bottle");
    ASSERT_TRUE(bottle);
    EXPECT_EQ(*bottle, (ItemRef{"minecraft:potion", 0}));
    EXPECT_EQ(t.fluid.count, 11);
    auto bucket = fillItem(t.fluid, "minecraft:bucket");
    ASSERT_TRUE(bucket);
    EXPECT_EQ(bucket->id, "minecraft:water_bucket");
    EXPECT_EQ(t.fluid.count, 7);
    EXPECT_FALSE(fillItem(t.fluid, "minecraft:bowl"));
}

TEST(JarFluids, PotionsKeepTheirKind) {
    auto swift     = fluidFromItem({"minecraft:potion", 14});
    auto slow      = fluidFromItem({"minecraft:potion", 17});
    auto lingering = fluidFromItem({"minecraft:lingering_potion", 14});
    ASSERT_TRUE(swift && slow && lingering);
    Tank t{{}, 12};
    EXPECT_EQ(t.add(swift->fluid, false), 1);
    EXPECT_EQ(t.add(slow->fluid, true), 0);
    EXPECT_EQ(t.add(lingering->fluid, true), 0);
    EXPECT_EQ(t.add(swift->fluid, false), 1);
    auto out = fillItem(t.fluid, "minecraft:glass_bottle");
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, (ItemRef{"minecraft:potion", 14}));
    EXPECT_FALSE(fluidFromItem({"minecraft:splash_potion", 0})); // splash water is not water
    EXPECT_EQ(contentFor({"potion", 1, 14, "minecraft:potion"}), kContentFirstPotion + 5);
    EXPECT_EQ(contentFor({"potion", 1, 1, "minecraft:potion"}), kContentFirstPotion);
}

TEST(JarFluids, AirContainers) {
    auto honeyBlock = fluidFromItem({"minecraft:honey_block", 0});
    ASSERT_TRUE(honeyBlock);
    EXPECT_EQ(honeyBlock->fluid, (FluidStack{"honey", 4}));
    EXPECT_TRUE(honeyBlock->emptyId.empty());
    FluidStack honey{"honey", 5};
    auto       block = fillItem(honey, "");
    ASSERT_TRUE(block);
    EXPECT_EQ(block->id, "minecraft:honey_block");
    EXPECT_EQ(honey.count, 1);
    EXPECT_FALSE(fillItem(honey, ""));
    auto bottle = fillItem(honey, "minecraft:glass_bottle");
    ASSERT_TRUE(bottle);
    EXPECT_EQ(bottle->id, "minecraft:honey_bottle");
    EXPECT_TRUE(honey.empty());
}

TEST(JarFluids, LevelsAndComparator) {
    EXPECT_EQ(
        (Tank{
            {"water", 12},
            12
    }
             .pixelLevel()),
        12
    );
    EXPECT_EQ(
        (Tank{
            {"water", 6},
            12
    }
             .pixelLevel()),
        6
    );
    EXPECT_EQ(
        (Tank{
            {"water", 1},
            24
    }
             .pixelLevel()),
        1
    );
    EXPECT_EQ((Tank{{}, 12}.comparatorOutput()), 0);
    EXPECT_EQ(
        (Tank{
            {"water", 6},
            12
    }
             .comparatorOutput()),
        8
    );
}

TEST(JarContents, JsonRoundTripAndVisuals) {
    Contents c;
    c.items = {
        {"minecraft:cookie", 0, ""    },
        {"minecraft:cookie", 0, "0a00"}
    };
    auto back = nlohmann::json(c).get<Contents>();
    EXPECT_EQ(back, c);
    EXPECT_EQ(visualFor(c, 12).content, kContentCookie);
    EXPECT_EQ(visualFor(c, 12).level, 2);
    Contents fish;
    fish.mob = StoredMob{"minecraft:cod", "Cod", "", "minecraft:cod_bucket", 20, 0, false};
    EXPECT_EQ(visualFor(fish, 12).content, 1);
    EXPECT_EQ(visualFor(fish, 12).level, 12);
    EXPECT_EQ(nlohmann::json(fish).get<Contents>(), fish);
    EXPECT_EQ(fromHex(toHex(std::string("\x00\xff\x10", 3))), std::string("\x00\xff\x10", 3));
}
