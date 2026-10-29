#ifdef CM_CLIENT

// Client renderer of U4 `sign`: draws the text of sign post arms, notice boards and doormats flat on
// their surfaces. The text travels in the display entities' name tags (vanilla clients show it as a
// floating name tag); here the name tag of those entities is replaced by depth-tested text objects
// oriented along the arm / board / mat, in the dye colour, with an outline when glowing.

#include <cmath>
#include <cstring>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/Font.h"
#include "mc/client/renderer/BaseActorRenderer.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/actor/ActorRenderer.h"
#include "mc/client/renderer/actor/DataDrivenRenderer.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Quaternion.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/minecraft_renderer/objects/NameTagRenderObject.h"
#include "mc/world/actor/Actor.h"

#include "feature/sign/SignData.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::client::sign {

namespace data = cm::feature::sign;

namespace {

struct V3 {
    float x, y, z;
};
V3   operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3   operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 toVec(V3 v) { return Vec3{v.x, v.y, v.z}; }

/// Bytes of an engaged `std::optional<Quaternion>` (the game's Quaternion has no exported
/// constructor, so the optional is built by layout).
struct OptQuat {
    alignas(4) unsigned char quat[16];
    bool engaged = true;
    unsigned char pad[3]{};

    [[nodiscard]] std::optional<Quaternion> const& get() const {
        return *reinterpret_cast<std::optional<Quaternion> const*>(this);
    }
};
static_assert(sizeof(std::optional<Quaternion>) == sizeof(OptQuat));
static_assert(alignof(std::optional<Quaternion>) == alignof(OptQuat));

/// Text frame: the glyphs run along +X (`right`), lines go down along -`up`, the readable side
/// faces `normal`.
OptQuat frameRotation(V3 right, V3 up, V3 normal) {
    glm::mat3 m(glm::vec3(right.x, right.y, right.z), glm::vec3(up.x, up.y, up.z), glm::vec3(normal.x, normal.y, normal.z));
    glm::quat q = glm::quat_cast(m);
    static_assert(sizeof(glm::quat) == 16);
    OptQuat out;
    std::memcpy(out.quat, &q, sizeof(q));
    return out;
}

mce::Color colorOf(std::uint32_t rgb) {
    return mce::Color{static_cast<int>((rgb >> 16) & 0xFF), static_cast<int>((rgb >> 8) & 0xFF), static_cast<int>(rgb & 0xFF), 255};
}

std::vector<std::string> splitLines(std::string const& s) {
    std::vector<std::string> out;
    std::size_t              pos = 0;
    while (true) {
        auto nl = s.find('\n', pos);
        out.push_back(data::stripFormatting(s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos)));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}

// Calibration of the name tag text objects (font pixels -> blocks).
constexpr float kPixel      = 1.0f / 16.0f;
constexpr float kLineHeight = 10.0f; ///< font pixels per line

struct TextJob {
    std::string text;      ///< lines joined by '\n'
    V3          center;    ///< world position of the text block centre
    V3          right, up, normal;
    float       scale;     ///< blocks per font pixel
    int         dye;
    bool        glowing;
};

class Builder {
public:
    Builder(BaseActorRenderer const& renderer, Font& font, Tessellator& tess, Vec3 const& cam)
    : mRenderer(renderer),
      mFont(font),
      mTess(tess),
      mCam(cam) {}

    void add(TextJob const& job, std::vector<NameTagRenderObject>& out) {
        if (job.text.empty()) return;
        std::vector<int> widths;
        mFont.calculateTextWidths(job.text, widths);
        int   lines   = 1 + static_cast<int>(std::count(job.text.begin(), job.text.end(), '\n'));
        // the object is anchored at the top centre of the text block
        V3    anchor  = job.center + job.up * (lines * kLineHeight * job.scale * 0.5f);
        auto  rot     = frameRotation(job.right, job.up, job.normal);
        if (job.glowing) {
            auto outline = colorOf(data::dyeOutlineColor(job.dye));
            for (auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
                V3 p = anchor + job.right * (dx * job.scale) + job.up * (dy * job.scale) + job.normal * (-0.001f);
                emit(job.text, widths, p, outline, job.scale, rot, out);
            }
        }
        emit(job.text, widths, anchor, colorOf(data::dyeTextColor(job.dye)), job.scale, rot, out);
    }

private:
    void emit(
        std::string const&                 text,
        std::vector<int> const&            widths,
        V3                                 pos,
        mce::Color const&                  color,
        float                              scale,
        OptQuat const&                     rot,
        std::vector<NameTagRenderObject>&  out
    ) {
        Vec3 rel{pos.x - mCam.x, pos.y - mCam.y, pos.z - mCam.z};
        auto objs =
            mRenderer.extractRenderTextObjects(mTess, text, widths, rel, color, scale, rot.get(), std::nullopt, true, false, false, 1.0f);
        for (auto& o : objs) out.push_back(std::move(o));
    }

    BaseActorRenderer const& mRenderer;
    Font&                    mFont;
    Tessellator&             mTess;
    Vec3                     mCam;
};

/// Readable side facing `normal` (horizontal): glyphs run to the reader's right.
V3 rightOf(V3 normal) { return {normal.z, 0.0f, -normal.x}; }

void signPostJobs(Actor& actor, std::string const& tag, std::vector<TextJob>& jobs) {
    auto const& pos   = actor.getPosition();
    data::Arm   arms[2] = {data::unpackArm(actor.getVariant()), data::unpackArm(actor.getMarkVariant())};
    auto        lines = splitLines(tag);
    std::size_t next  = 0;
    for (int i = 0; i < 2; ++i) {
        auto const& arm = arms[i];
        if (!arm.active) continue;
        std::string text = next < lines.size() ? lines[next] : std::string{};
        ++next;
        if (text.empty()) continue;
        float rad = arm.yaw * std::numbers::pi_v<float> / 180.0f;
        V3    tip{-std::sin(rad), 0.0f, std::cos(rad)};
        float dy     = i == 0 ? -4.0f : -11.0f;
        V3    center = V3{pos.x, pos.y + dy * kPixel, pos.z} + tip * (2.0f * kPixel);
        V3    n1{std::cos(rad), 0.0f, std::sin(rad)};
        for (V3 n : {n1, n1 * -1.0f}) {
            jobs.push_back(TextJob{text, center + n * (0.5f * kPixel + 0.004f), rightOf(n), V3{0, 1, 0}, n, 0.0095f, arm.color, arm.glowing});
        }
    }
}

void textDisplayJobs(Actor& actor, std::string const& tag, std::vector<TextJob>& jobs) {
    auto const& pos   = actor.getPosition();
    auto        info  = data::unpackDisplay(actor.getVariant());
    auto        lines = splitLines(tag);
    std::string text;
    int         widest = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) text += '\n';
        text += lines[i];
        widest = std::max(widest, data::textWidth(lines[i]));
    }
    auto [dx, dz] = game::facingOffset(info.facing);
    V3 facing{static_cast<float>(dx), 0.0f, static_cast<float>(dz)};
    if (info.kind == data::DisplayKind::Doormat) {
        // flat on the mat; the top of the text points away from the reader (who stands on the facing side)
        V3    up = facing * -1.0f;
        V3    right = rightOf(facing);
        float scale = std::min(0.0105f, 0.80f / static_cast<float>(std::max(widest, 1)));
        jobs.push_back(TextJob{text, V3{pos.x, pos.y + 0.004f, pos.z}, right, up, V3{0, 1, 0}, scale, info.color, info.glowing});
        return;
    }
    // notice board: on the front face, shrunk to fit inside the frame
    int   n     = static_cast<int>(lines.size());
    float fit   = 0.72f; // usable face size in blocks
    float scale = std::min({0.0105f, fit / static_cast<float>(std::max(widest, 1)), fit / (n * kLineHeight)});
    jobs.push_back(TextJob{text, V3{pos.x, pos.y, pos.z} + facing * 0.004f, rightOf(facing), V3{0, 1, 0}, facing, scale, info.color, info.glowing});
}

BaseActorRenderer const* anyRenderer(ActorRenderDispatcher& dispatcher) {
    for (auto const& [name, renderer] : *dispatcher.mDataDrivenRenderers) {
        if (renderer) return static_cast<BaseActorRenderer const*>(renderer.get());
    }
    return nullptr;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    SignTextHook,
    ll::memory::HookPriority::Normal,
    ActorRenderDispatcher,
    &ActorRenderDispatcher::extractRenderTextObjects,
    std::vector<NameTagRenderObject>,
    Font&              font,
    Tessellator&       tessellator,
    Actor&             actor,
    std::string const& str,
    Vec3               camTargetPos,
    float              a,
    mce::Color         color
) {
    auto type = actor.getTypeName();
    if (type != data::kSignPostEntity && type != data::kTextDisplayEntity) {
        return origin(font, tessellator, actor, str, camTargetPos, a, color);
    }
    std::vector<NameTagRenderObject> out;
    try {
        auto const* renderer = anyRenderer(*this);
        if (!renderer) return origin(font, tessellator, actor, str, camTargetPos, a, color);
        std::vector<TextJob> jobs;
        if (type == data::kSignPostEntity) signPostJobs(actor, str, jobs);
        else textDisplayJobs(actor, str, jobs);
        Builder builder(*renderer, font, tessellator, camTargetPos);
        for (auto const& job : jobs) builder.add(job, out);
    } catch (std::exception const& e) {
        static bool logged = false;
        if (!logged) log().error("sign: text rendering failed: {}", e.what());
        logged = true;
    }
    return out;
}

struct SignClientModule {
    void install() {
        SignTextHook::hook();
        log().info("sign: client text renderer installed");
    }
    void uninstall() { SignTextHook::unhook(); }
};

} // namespace cm::client::sign

CM_REGISTER_MODULE(cm::client::sign::SignClientModule);

#endif // CM_CLIENT
