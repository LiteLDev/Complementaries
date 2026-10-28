#pragma once

// Pure maths of the cannon (no game headers, unit-tested in tests/feature/CannonMathTest.cpp):
// the barrel's local frame on each face, angle conversions and the projectile path prediction.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace cm::feature::cannon::math {

struct V3 {
    double x = 0, y = 0, z = 0;

    friend V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    [[nodiscard]] double dot(V3 o) const { return x * o.x + y * o.y + z * o.z; }
    [[nodiscard]] V3     cross(V3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    [[nodiscard]] double length() const { return std::sqrt(dot(*this)); }
    [[nodiscard]] V3     normalized() const {
        double l = length();
        return l > 1e-12 ? *this * (1.0 / l) : V3{0, 1, 0};
    }
};

inline constexpr double kPi = std::numbers::pi;
inline double           toRad(double deg) { return deg * kPi / 180.0; }
inline double           toDeg(double rad) { return rad * 180.0 / kPi; }

/// Barrel limits (degrees): elevation above the base plane.
inline constexpr double kMinPitch = -20.0;
inline constexpr double kMaxPitch = 90.0;

/// Faces in Bedrock FacingID order: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east. A cannon's
/// `minecraft:block_face` is the face it was placed against, so its barrel points away from the
/// base along this direction's normal ("up" = standing on the floor).
inline V3 faceNormal(int face) {
    switch (face) {
    case 0:
        return {0, -1, 0};
    case 2:
        return {0, 0, -1};
    case 3:
        return {0, 0, 1};
    case 4:
        return {-1, 0, 0};
    case 5:
        return {1, 0, 0};
    default:
        return {0, 1, 0};
    }
}

/// Local frame of a cannon: `n` away from the base, `f` = yaw 0, `r` = yaw +90.
/// Floor cannons use Minecraft's yaw convention (0 = south, 90 = west).
struct Basis {
    V3 n, f, r;
};
inline Basis basis(int face) {
    V3 n = faceNormal(face);
    V3 f = (std::abs(n.y) > 0.5) ? V3{0, 0, 1} : V3{0, 1, 0};
    return {n, f, f.cross(n)};
}

inline double wrapYaw(double yaw) {
    yaw = std::fmod(yaw + 180.0, 360.0);
    if (yaw < 0) yaw += 360.0;
    return yaw - 180.0;
}
inline double clampPitch(double pitch) { return std::clamp(pitch, kMinPitch, kMaxPitch); }

/// Unit direction of the barrel for a local (yaw, pitch) in degrees.
inline V3 localToWorld(int face, double yawDeg, double pitchDeg) {
    auto   b = basis(face);
    double y = toRad(yawDeg), p = toRad(pitchDeg);
    return (b.f * (std::cos(y) * std::cos(p)) + b.r * (std::sin(y) * std::cos(p)) + b.n * std::sin(p)).normalized();
}

/// Local (yaw, pitch) of a world direction (not clamped).
inline std::pair<double, double> worldToLocal(int face, V3 dir) {
    auto b = basis(face);
    dir    = dir.normalized();
    double pitch = toDeg(std::asin(std::clamp(dir.dot(b.n), -1.0, 1.0)));
    double yaw   = toDeg(std::atan2(dir.dot(b.r), dir.dot(b.f)));
    if (std::abs(pitch) > 89.999) yaw = 0;
    return {wrapYaw(yaw), pitch};
}

/// Minecraft yaw (0 = +z, 90 = -x) and elevation (up = positive) of a world direction.
inline std::pair<double, double> worldAngles(V3 dir) {
    dir = dir.normalized();
    double yaw   = toDeg(std::atan2(-dir.x, dir.z));
    double pitch = toDeg(std::asin(std::clamp(dir.y, -1.0, 1.0)));
    return {wrapYaw(yaw), pitch};
}

/// Bedrock projectile integration (ProjectileComponent): every tick the position moves by the
/// velocity, then the velocity is multiplied by the drag (inertia) and loses `gravity` in y.
/// `hit(from, to)` returns the fraction (0..1) of the segment where the path is blocked, or a value
/// > 1 when it is free. Returns the visited points (start first, the hit point last if any).
template <class HitFn>
std::vector<V3> trajectory(V3 pos, V3 vel, double gravity, double drag, int maxTicks, HitFn&& hit, bool* blocked = nullptr) {
    std::vector<V3> pts{pos};
    if (blocked) *blocked = false;
    for (int t = 0; t < maxTicks; ++t) {
        V3     next = pos + vel;
        double f    = hit(pos, next);
        if (f <= 1.0) {
            pts.push_back(pos + vel * std::max(0.0, f));
            if (blocked) *blocked = true;
            return pts;
        }
        pos = next;
        pts.push_back(pos);
        vel   = vel * drag;
        vel.y -= gravity;
    }
    return pts;
}

/// Unit vectors spread evenly over the sphere (Fibonacci lattice), for shrapnel.
inline std::vector<V3> sphereDirections(int n) {
    std::vector<V3> out;
    if (n <= 0) return out;
    out.reserve(static_cast<size_t>(n));
    double const golden = kPi * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; ++i) {
        double y = n == 1 ? 0.0 : 1.0 - 2.0 * (i + 0.5) / n;
        double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        double a = golden * i;
        out.push_back({std::cos(a) * r, y, std::sin(a) * r});
    }
    return out;
}


/// Kinetic "energy" of a cannonball (Supplementaries: proportional to speed squared).
inline double energy(double powerScaling, V3 velocity) { return powerScaling * velocity.dot(velocity); }
/// Speed that carries `e` energy.
inline double speedFor(double powerScaling, double e) { return powerScaling > 0 && e > 0 ? std::sqrt(e / powerScaling) : 0.0; }

/// Ray (origin, unit dir) vs axis-aligned box: entry distance, or a negative value on a miss.
inline double rayBox(V3 o, V3 d, V3 mn, V3 mx) {
    double tmin = 0.0, tmax = 1e30;
    double const oc[3] = {o.x, o.y, o.z}, dc[3] = {d.x, d.y, d.z}, lo[3] = {mn.x, mn.y, mn.z}, hi[3] = {mx.x, mx.y, mx.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(dc[i]) < 1e-12) {
            if (oc[i] < lo[i] || oc[i] > hi[i]) return -1.0;
            continue;
        }
        double t1 = (lo[i] - oc[i]) / dc[i], t2 = (hi[i] - oc[i]) / dc[i];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return -1.0;
    }
    return tmin;
}

} // namespace cm::feature::cannon::math
