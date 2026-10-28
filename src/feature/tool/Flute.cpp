// Flute: plays a tune that calls the player's pets (all of them nearby, or the bound one from far
// away) and makes parrots dance. Using it on one of your pets binds the flute to it.

#include <cmath>
#include <string>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/phys/AABB.h"

#include "feature/tool/Tool.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::tool::flute {

namespace {

constexpr int kDanceTicks    = 200;
constexpr int kCooldownTicks = 20;

struct Note {
    int          dim;
    std::int64_t player;
    int          step;
};
struct Dancer {
    std::int64_t  actor;
    std::uint64_t until;
};

std::vector<Note>& notes() {
    static std::vector<Note> v;
    return v;
}
std::vector<Dancer>& dancers() {
    static std::vector<Dancer> v;
    return v;
}

std::uint64_t now() { return currentTick(); }

std::string boundPrefix() {
    // lore uses the server language (like the other tool lore lines) so it can be found again
    auto text = tr("message.supplementaries.flute.bound", "Bound to: {}", "");
    return fmtCode("r7") + text;
}

std::string petName(Actor& pet) {
    auto const& tag = pet.getNameTag();
    if (!tag.empty()) return tag;
    return pet.getEntityLocNameString();
}

/// Puts `pet` next to `player`, standing.
void bringPet(Player& player, Actor& pet, int index) {
    float angle = static_cast<float>(index) * 2.4f;
    Vec3  p     = player.getPosition();
    Vec3  to{p.x + std::cos(angle) * 1.5f, p.y, p.z + std::sin(angle) * 1.5f};
    if (pet.isSitting()) pet.setSitting(false);
    pet.teleport(to, player.getDimensionId());
    particle(static_cast<int>(player.getDimensionId()), to + Vec3{0, 0.8f, 0}, "minecraft:note_particle");
}

bool ownedBy(Actor& pet, Player& player) {
    return pet.isTame() && pet.getOwnerId().rawID == player.getOrCreateUniqueID().rawID;
}

} // namespace

int callPets(Player& player, ItemStack& fluteItem) {
    int  dim   = static_cast<int>(player.getDimensionId());
    int  count = 0;
    auto bound = stringData(fluteItem, "cm_flute_pet");
    if (!bound.empty()) {
        std::int64_t id = 0;
        try {
            id = std::stoll(bound);
        } catch (...) {
        }
        auto* pet = id ? actorById(id) : nullptr;
        if (pet && pet->getDimensionId() == player.getDimensionId()
            && pet->getPosition().distanceTo(player.getPosition()) <= static_cast<float>(config().flute.boundDistance)) {
            bringPet(player, *pet, 0);
            ++count;
        }
    } else {
        float r = static_cast<float>(std::max(1, config().flute.unboundRadius));
        Vec3  p = player.getPosition();
        AABB  box{Vec3{p.x - r, p.y - r, p.z - r}, Vec3{p.x + r, p.y + r, p.z + r}};
        std::vector<Actor*> pets;
        for (Actor* a : player.getDimensionBlockSource().fetchEntities(&player, box, false, false)) {
            if (!a->isAlive()) continue;
            if (a->isType(ActorType::Parrot)) {
                SynchedActorDataAccess::setActorFlag(a->getEntityContext(), ActorFlags::Dancing, true);
                dancers().push_back({a->getOrCreateUniqueID().rawID, now() + kDanceTicks});
            }
            if (ownedBy(*a, player)) pets.push_back(a);
        }
        for (auto* pet : pets) bringPet(player, *pet, count++);
    }
    notes().push_back({dim, player.getOrCreateUniqueID().rawID, 0});
    log().info("flute called {} pet(s) for {}", count, player.getRealName());
    return count;
}

bool useInAir(Player& player) {
    if (!config().flute.enabled) return false;
    ItemStack item = player.getSelectedItem();
    if (!isItem(item, ids::Flute)) return false;
    HashedString const category("complementaries:flute");
    if (player.isItemOnCooldown(category)) return false;
    player.startItemCooldown(category, kCooldownTicks, true);
    if (player.isSneaking() && !stringData(item, "cm_flute_pet").empty()) {
        removeData(item, "cm_flute_pet");
        setLoreLine(item, boundPrefix(), "");
        setMainhand(player, item);
        player.sendMessage(trFor(player, "message.supplementaries.flute.unbound", "Flute unbound"));
        return true;
    }
    callPets(player, item);
    damageMainhand(player, 1);
    return true;
}

bool useOnEntity(Player& player, Actor& target) {
    if (!config().flute.enabled || !target.isTame()) return false;
    if (!ownedBy(target, player)) {
        player.sendMessage(trFor(player, "message.supplementaries.flute.not_your_pet", "This is not your pet"));
        return true;
    }
    ItemStack item = player.getSelectedItem();
    if (!isItem(item, ids::Flute)) return false;
    auto name = petName(target);
    setStringData(item, "cm_flute_pet", std::to_string(target.getOrCreateUniqueID().rawID));
    setLoreLine(item, boundPrefix(), boundPrefix() + name);
    setMainhand(player, item);
    player.sendMessage(trFor(player, "message.supplementaries.flute.bound", "Bound to: {}", name));
    particle(static_cast<int>(target.getDimensionId()), target.getPosition() + Vec3{0, 1.0f, 0}, "minecraft:heart_particle");
    log().info("flute bound to {} by {}", name, player.getRealName());
    return true;
}

void tick() {
    if (!notes().empty()) {
        static constexpr float pitches[] = {1.0f, 1.26f, 1.5f, 1.12f, 1.68f};
        for (auto& n : notes()) {
            if (auto* p = playerById(n.player)) {
                if (n.step % 3 == 0) {
                    int i = n.step / 3;
                    playSound(n.dim, eyePos(*p), "note.flute", 1.0f, pitches[i]);
                    particle(n.dim, eyePos(*p) + lookVector(*p) * 0.5f + Vec3{0, 0.3f, 0}, "minecraft:note_particle");
                }
            }
            ++n.step;
        }
        std::erase_if(notes(), [](Note const& n) { return n.step >= 15; });
    }
    if (!dancers().empty()) {
        auto t = now();
        for (auto const& d : dancers()) {
            if (d.until > t) continue;
            if (auto* a = actorById(d.actor)) SynchedActorDataAccess::setActorFlag(a->getEntityContext(), ActorFlags::Dancing, false);
        }
        std::erase_if(dancers(), [t](Dancer const& d) { return d.until <= t; });
    }
}

void clear() {
    notes().clear();
    dancers().clear();
}

} // namespace cm::feature::tool::flute
