#ifdef CM_CLIENT

// Client side of U11 `rope`: the client predicts the player's movement, so it must know that ropes
// are climbable. Custom block types of a remote server are registered when joining it, so the flag
// is set again on every join (local worlds share the server's registry, which the server module
// already marked).

#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "feature/rope/Common.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace cm::client::rope {

class RopeClientModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ClientJoinLevelEvent>([this](auto&) {
            mPending = 200; // the block registry may be finished a few ticks after the join
            mark();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([this](auto&) {
            if (mPending > 0) {
                --mPending;
                mark();
            }
        }));
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    void mark() {
        if (cm::feature::rope::markRopeClimbable()) {
            if (mPending > 0) log().info("rope: ropes are climbable on this client");
            mPending = 0;
        }
    }

    std::vector<ll::event::ListenerPtr> mListeners;
    int                                 mPending = 0;
};

} // namespace cm::client::rope

CM_REGISTER_MODULE(cm::client::rope::RopeClientModule);

#endif // CM_CLIENT
