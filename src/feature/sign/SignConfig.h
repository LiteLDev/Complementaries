#pragma once

namespace cm::feature::sign {

/// `config/sign.json`.
struct SignConfig {
    int    version                   = 1;
    bool   signPostEnabled           = true;
    bool   noticeBoardEnabled        = true;
    bool   noticeBoardAcceptsAnyItem = false;
    bool   doormatEnabled            = true;
    bool   waySigns                  = true;
    double waySignChance             = 0.25;
    int    waySignMinDistance        = 32;
    int    waySignMaxDistance        = 256;
    bool   waySignDistanceText       = true;
};

} // namespace cm::feature::sign
