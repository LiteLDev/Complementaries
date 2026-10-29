#include <gtest/gtest.h>

#include "feature/mechanism/RotationRules.h"

namespace rot = cm::feature::mechanism::rotation;
using rot::Result;
using rot::Value;

TEST(MechanismRotation, ClockwiseFromAboveTurnsNorthToEast) {
    EXPECT_EQ(rot::rotateDir(rot::kNorth, rot::kUp, false), rot::kEast);
    EXPECT_EQ(rot::rotateDir(rot::kEast, rot::kUp, false), rot::kSouth);
    EXPECT_EQ(rot::rotateDir(rot::kSouth, rot::kUp, false), rot::kWest);
    EXPECT_EQ(rot::rotateDir(rot::kWest, rot::kUp, false), rot::kNorth);
    EXPECT_EQ(rot::rotateDir(rot::kNorth, rot::kUp, true), rot::kWest);
    // seen from below, clockwise is counter-clockwise seen from above
    EXPECT_EQ(rot::rotateDir(rot::kNorth, rot::kDown, false), rot::kWest);
    // the axis itself never moves
    EXPECT_EQ(rot::rotateDir(rot::kUp, rot::kUp, false), rot::kUp);
    EXPECT_EQ(rot::rotateDir(rot::kDown, rot::kUp, true), rot::kDown);
}

TEST(MechanismRotation, HorizontalAxis) {
    // a table facing north turns up -> west -> down -> east (clockwise seen from the north side)
    int d = rot::kUp;
    d     = rot::rotateDir(d, rot::kNorth, false);
    EXPECT_EQ(d, rot::kWest);
    d = rot::rotateDir(d, rot::kNorth, false);
    EXPECT_EQ(d, rot::kDown);
    d = rot::rotateDir(d, rot::kNorth, false);
    EXPECT_EQ(d, rot::kEast);
    d = rot::rotateDir(d, rot::kNorth, false);
    EXPECT_EQ(d, rot::kUp);
}

TEST(MechanismRotation, FourTurnsAreIdentity) {
    for (int axis = 0; axis < 6; ++axis) {
        for (int d = 0; d < 6; ++d) {
            for (bool ccw : {false, true}) {
                int r = d;
                for (int i = 0; i < 4; ++i) r = rot::rotateDir(r, axis, ccw);
                EXPECT_EQ(r, d) << "axis " << axis << " dir " << d;
                EXPECT_EQ(rot::rotateDir(rot::rotateDir(d, axis, ccw), axis, !ccw), d);
            }
        }
    }
}

TEST(MechanismRotation, FacingStates) {
    Value v = 2; // north
    EXPECT_EQ(rot::rotateState("facing_direction", v, rot::kUp, false), Result::Changed);
    EXPECT_EQ(std::get<int>(v), rot::kEast);

    Value s = std::string("north");
    EXPECT_EQ(rot::rotateState("minecraft:cardinal_direction", s, rot::kUp, false), Result::Changed);
    EXPECT_EQ(std::get<std::string>(s), "east");
    s = std::string("east");
    EXPECT_EQ(rot::rotateState("minecraft:cardinal_direction", s, rot::kNorth, false), Result::Unsupported);
    s = std::string("north");
    EXPECT_EQ(rot::rotateState("minecraft:cardinal_direction", s, rot::kNorth, false), Result::Unchanged);

    Value f = std::string("up");
    EXPECT_EQ(rot::rotateState("minecraft:facing_direction", f, rot::kNorth, false), Result::Changed);
    EXPECT_EQ(std::get<std::string>(f), "west");
}

TEST(MechanismRotation, LegacyHorizontalTables) {
    Value d = 2; // direction: north
    EXPECT_EQ(rot::rotateState("direction", d, rot::kUp, false), Result::Changed);
    EXPECT_EQ(std::get<int>(d), 3); // east
    Value w = 3;                    // stairs: north
    EXPECT_EQ(rot::rotateState("weirdo_direction", w, rot::kUp, false), Result::Changed);
    EXPECT_EQ(std::get<int>(w), 0); // east
}

TEST(MechanismRotation, Axes) {
    Value a = std::string("x");
    EXPECT_EQ(rot::rotateState("pillar_axis", a, rot::kUp, false), Result::Changed);
    EXPECT_EQ(std::get<std::string>(a), "z");
    Value y = std::string("y");
    EXPECT_EQ(rot::rotateState("pillar_axis", y, rot::kUp, false), Result::Unchanged);
    EXPECT_EQ(rot::rotateState("pillar_axis", y, rot::kEast, false), Result::Changed);
    EXPECT_EQ(std::get<std::string>(y), "z");
}

TEST(MechanismRotation, SignsRailsAndBits) {
    Value sign = 0; // south
    EXPECT_EQ(rot::rotateState("ground_sign_direction", sign, rot::kUp, false), Result::Changed);
    EXPECT_EQ(std::get<int>(sign), 4); // west
    Value sign2 = 0;
    EXPECT_EQ(rot::rotateState("ground_sign_direction", sign2, rot::kUp, true), Result::Changed);
    EXPECT_EQ(std::get<int>(sign2), 12);
    Value s3 = 0;
    EXPECT_EQ(rot::rotateState("ground_sign_direction", s3, rot::kNorth, true), Result::Unsupported);

    Value rail = 6; // south-east curve
    EXPECT_EQ(rot::rotateState("rail_direction", rail, rot::kUp, false), Result::Changed);
    EXPECT_EQ(std::get<int>(rail), 7); // south-west
    Value straight = 0;
    EXPECT_EQ(rot::rotateState("rail_direction", straight, rot::kUp, true), Result::Changed);
    EXPECT_EQ(std::get<int>(straight), 1);

    Value vine = 1; // south
    EXPECT_EQ(rot::rotateState("vine_direction_bits", vine, rot::kUp, false), Result::Changed);
    EXPECT_EQ(std::get<int>(vine), 2); // west
}

TEST(MechanismRotation, UnrelatedStatesStay) {
    Value v = 5;
    EXPECT_EQ(rot::rotateState("age", v, rot::kUp, false), Result::Unchanged);
    EXPECT_EQ(std::get<int>(v), 5);
    EXPECT_TRUE(rot::blocksRotation("head_piece_bit"));
    EXPECT_FALSE(rot::blocksRotation("age"));
}
