// Shared pieces of the lock unit: block kinds and states, feedback, keys, lock data, scheduling.

#include "feature/lock/Lock.h"

#include <algorithm>
#include <map>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/level/Spawner.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace cm::feature::lock {

LockConfig const& config() { return cm::config::get<LockConfig>(); }

// ---- kinds -----------------------------------------------------------------------------------------

Kind kindOf(std::string_view id) {
    constexpr std::string_view prefix = "complementaries:";
    if (!id.starts_with(prefix)) return Kind::None;
    if (id == kLockBlockId) return Kind::LockBlock;
    if (id == kSafeId) return Kind::Safe;
    if (id == kIronGateId) return Kind::IronGate;
    if (id == kGoldGateId) return Kind::GoldGate;
    if (id == kGoldDoorId) return Kind::GoldDoor;
    if (id == kNetheriteDoorId) return Kind::NetheriteDoor;
    if (id == kGoldTrapdoorId) return Kind::GoldTrapdoor;
    if (id == kNetheriteTrapdoorId) return Kind::NetheriteTrapdoor;
    return Kind::None;
}

Kind kindOf(Block const& block) { return kindOf(std::string_view(block.getTypeName())); }

bool enabled(Kind k) {
    auto const& c = config();
    switch (k) {
    case Kind::LockBlock:
        return c.enableLockBlock;
    case Kind::Safe:
        return c.enableSafe;
    case Kind::IronGate:
    case Kind::GoldGate:
        return c.enableIronGate;
    case Kind::GoldDoor:
        return c.enableGoldDoor;
    case Kind::NetheriteDoor:
        return c.enableNetheriteDoor;
    case Kind::GoldTrapdoor:
        return c.enableGoldTrapdoor;
    case Kind::NetheriteTrapdoor:
        return c.enableNetheriteTrapdoor;
    default:
        return false;
    }
}

// ---- states ----------------------------------------------------------------------------------------

int state(Block const& block, std::string_view name, int fallback) {
    auto const* s = block.getBlockType().getBlockState(HashedString(std::string(name)));
    if (!s) return fallback;
    return block.getState<int>(*s).value_or(fallback);
}

bool flag(Block const& block, std::string_view name) { return state(block, name, 0) != 0; }

Block const& with(Block const& block, std::vector<std::pair<std::string_view, int>> const& states) {
    Block const* cur = &block;
    for (auto const& [name, value] : states) {
        auto const* s = cur->getBlockType().getBlockState(HashedString(std::string(name)));
        if (!s) continue;
        if (auto next = cur->setState<int>(*s, value)) cur = next.as_ptr();
    }
    return *cur;
}

bool place(BlockSource& region, BlockPos const& pos, Block const& block) { return game::setBlock(region, pos, block); }

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return nullptr;
    return &d->getBlockSourceFromMainChunkSource();
}

ItemStack makeItem(std::string_view id, int count) {
    std::string snbt = "{\"Name\":\"" + std::string(id) + "\",\"Count\":" + std::to_string(count) + "b,\"Damage\":0s}";
    auto        tag  = CompoundTag::fromSnbt(snbt);
    if (!tag) return ItemStack{};
    return ItemStack::fromTag(*tag);
}

void dropItem(BlockSource& region, Vec3 const& pos, ItemStack const& item) {
    if (item.isNull()) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    level->getSpawner().spawnItem(region, item, nullptr, pos, 10);
}

void giveItem(Player& player, ItemStack const& item) {
    if (item.isNull()) return;
    ItemStack copy = item;
    if (!player.addAndRefresh(copy) && !copy.isNull() && copy.mCount > 0) {
        dropItem(player.getDimensionBlockSource(), player.getPosition(), copy);
    }
}

// ---- feedback --------------------------------------------------------------------------------------

void actionBar(Player& player, std::string const& text) {
    SetTitlePacket pkt(SetTitlePacketPayload(SetTitlePacketPayload::TitleType::Actionbar, text, std::nullopt));
    pkt.sendTo(player);
}

void playSound(int dim, Vec3 const& pos, std::string const& name, float volume, float pitch) {
    PlaySoundPacket pkt;
    pkt.mName   = name;
    pkt.mPos    = BlockPos{
        static_cast<int>(pos.x * 8.0f),
        static_cast<int>(pos.y * 8.0f),
        static_cast<int>(pos.z * 8.0f),
    };
    pkt.mVolume    = volume;
    pkt.mPitch     = pitch;
    pkt.mLoopCount = 0;
    pkt.sendTo(game::blockPosOf(pos), DimensionType(dim));
}

void playSound(int dim, BlockPos const& pos, std::string const& name, float volume, float pitch) {
    playSound(dim, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, name, volume, pitch);
}

float randomFloat() {
    static std::mt19937                          rng{std::random_device{}()};
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}

// ---- keys ------------------------------------------------------------------------------------------

std::optional<std::string> keyPassword(ItemStack const& stack) {
    if (stack.isNull() || stack.mCount == 0) return std::nullopt;
    if (stack.getTypeName() != kKeyId) return std::nullopt;
    auto name = stack.getCustomName();
    if (name.empty()) return std::string("Key"); // the untranslated item name, as in Java
    return name;
}

KeyStatus keyStatus(ItemStack const& stack, std::optional<std::string> const& password) {
    auto pw = keyPassword(stack);
    if (!pw) return KeyStatus::NoKey;
    return (password && *pw == *password) ? KeyStatus::Correct : KeyStatus::Incorrect;
}

KeyStatus inventoryKeyStatus(Player& player, std::optional<std::string> const& password) {
    if (!password) return KeyStatus::Correct;
    KeyStatus found = KeyStatus::NoKey;
    auto      visit = [&](ItemStack const& s) {
        auto st = keyStatus(s, password);
        if (st == KeyStatus::Correct) return true;
        if (st == KeyStatus::Incorrect) found = KeyStatus::Incorrect;
        return false;
    };
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (visit(inv.getItem(i))) return KeyStatus::Correct;
    }
    using Slot = SharedTypes::Legacy::EquipmentSlot;
    for (auto slot : {Slot::Offhand, Slot::Head, Slot::Torso, Slot::Legs, Slot::Feet}) {
        if (visit(player.getEquippedSlot(slot))) return KeyStatus::Correct;
    }
    return found;
}

KeyStatus playerKeyStatus(Player& player, std::optional<std::string> const& password) {
    if (player.isCreative()) return KeyStatus::Correct;
    return inventoryKeyStatus(player, password);
}

void sendKeyMessage(Player& player, KeyStatus status, std::string_view what) {
    if (status == KeyStatus::Incorrect) {
        actionBar(player, trFor(player, "message.supplementaries.safe.incorrect_key", "You do not have the correct key"));
    } else if (status == KeyStatus::NoKey) {
        std::string key = "message.supplementaries." + std::string(what) + ".locked";
        std::string fallback = what == "door" ? "This door is locked"
                             : what == "trapdoor" ? "This trapdoor is locked"
                             : what == "safe" ? "This safe is locked"
                                              : "This block is locked";
        actionBar(player, trFor(player, key, fallback));
    }
}

// ---- lock data -------------------------------------------------------------------------------------

namespace {
std::string lockKey(core::GlobalPos const& pos) { return "cm:lock:lock:" + pos.key(); }
} // namespace

std::optional<std::string> password(core::GlobalPos const& pos) {
    auto v = game::db::load(lockKey(pos));
    if (!v || !v->is_object() || !v->contains("password") || !(*v)["password"].is_string()) return std::nullopt;
    return (*v)["password"].get<std::string>();
}

void setPassword(core::GlobalPos const& pos, std::optional<std::string> const& pw) {
    if (!pw) {
        game::db::erase(lockKey(pos));
        return;
    }
    game::db::save(lockKey(pos), nlohmann::json{{"password", *pw}});
}

void eraseLockData(core::GlobalPos const& pos) { game::db::erase(lockKey(pos)); }

bool handleKeyAction(Player& player, ItemStack const& held, bool sneaking, core::GlobalPos const& lockPos, std::string_view what) {
    if (player.isSpectator()) return false;
    auto keyPw = keyPassword(held);
    auto pw    = password(lockPos);
    auto bpos  = game::blockPos(lockPos);
    if (sneaking && keyPw) {
        // clear ownership
        if (player.isCreative() || keyStatus(held, pw) == KeyStatus::Correct) {
            setPassword(lockPos, std::nullopt);
            actionBar(player, trFor(player, "message.supplementaries.safe.cleared", "Cleared ownership"));
            playSound(lockPos.dim, bpos, "complementaries.lock.iron_trapdoor.open", 0.5f, 1.5f);
            log().info("lock: {} cleared the key of {} at {}", player.getRealName(), what, lockPos.key());
            return false;
        }
    } else if (!pw) {
        if (keyPw) {
            setPassword(lockPos, keyPw);
            if (keyPw->size() <= kMaxShownPassword) {
                actionBar(player, trFor(player, "message.supplementaries.safe.assigned_key", "Assigned to '{}' key", *keyPw));
            } else {
                actionBar(player, trFor(player, "message.supplementaries.safe.assigned_key_generic", "Assigned to key"));
            }
            playSound(lockPos.dim, bpos, "complementaries.lock.iron_trapdoor.open", 0.5f, 1.5f);
            log().info("lock: {} assigned key '{}' to {} at {}", player.getRealName(), *keyPw, what, lockPos.key());
            return false;
        }
        return true;
    }
    auto status = playerKeyStatus(player, pw);
    sendKeyMessage(player, status, what);
    return status == KeyStatus::Correct;
}

// ---- scheduling ------------------------------------------------------------------------------------

namespace {
struct Scheduled {
    long long             due;
    std::function<void()> fn;
};
long long              gTick = 0;
std::vector<Scheduled> gScheduled;
} // namespace

void defer(std::function<void()> fn) { later(1, std::move(fn)); }

void later(int ticks, std::function<void()> fn) { gScheduled.push_back({gTick + std::max(ticks, 1), std::move(fn)}); }

namespace detail {
void runScheduled() {
    ++gTick;
    if (gScheduled.empty()) return;
    std::vector<Scheduled> due;
    for (auto it = gScheduled.begin(); it != gScheduled.end();) {
        if (it->due <= gTick) {
            due.push_back(std::move(*it));
            it = gScheduled.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& s : due) {
        try {
            s.fn();
        } catch (std::exception const& e) {
            log().error("lock: scheduled task failed: {}", e.what());
        }
    }
}
void clearScheduled() { gScheduled.clear(); }
long long currentTick() { return gTick; }
} // namespace detail

} // namespace cm::feature::lock
