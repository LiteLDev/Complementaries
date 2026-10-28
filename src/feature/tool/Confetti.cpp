// Confetti popper: a one-shot burst of confetti in the look direction (or a dispenser's facing).

#include "mc/world/actor/player/Player.h"

#include "feature/tool/Tool.h"
#include "mod/Log.h"

namespace cm::feature::tool::confetti {

void burst(int dim, Vec3 const& from, Vec3 const& direction) {
    Vec3 dir = normalized(direction);
    // a cone: small bursts further and further along the direction
    for (float d : {0.4f, 0.9f, 1.5f, 2.2f}) particle(dim, from + dir * d, "complementaries:confetti");
    playSound(dim, from, "firework.blast", 1.0f, 1.3f);
    playSound(dim, from, "random.pop", 1.0f, 0.8f);
}

bool useInAir(Player& player) {
    if (!config().confettiPopper.enabled) return false;
    burst(static_cast<int>(player.getDimensionId()), eyePos(player) - Vec3{0, 0.2f, 0}, lookVector(player));
    consumeMainhand(player, 1);
    log().info("confetti popper used by {}", player.getRealName());
    return true;
}

} // namespace cm::feature::tool::confetti
