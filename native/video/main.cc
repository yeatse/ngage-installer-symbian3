// MMF video forwarding that keeps the posting surface off the window while a video player is created.
#include <e32std.h>
#include <e32svr.h>
#include <e32property.h>
#include <graphics/surfaceconfiguration.h>
#include <new>
#include "../common/video_window.h"

CBase::~CBase() {}
void operator delete(void* pointer) noexcept { User::Free(pointer); }
void operator delete(void* pointer, unsigned) noexcept { User::Free(pointer); }
extern "C" void __cxa_pure_virtual() { User::Invariant(); }
extern "C" void _E32Startup();

namespace {
enum { kSetDisplayWindowL = 13, kNewL = 20, kClose = 24, kOpenDesL = 27, kOpenFileL = 30 };
enum { kGetBackgroundSurface = 596, kSetBackgroundSurface = 598, kRemoveBackgroundSurface = 600 };
struct State {
    RLibrary video;
    RLibrary window;
    TAny* game = nullptr;
    TSurfaceConfiguration display;
    bool restore = false;
};
State& Session() {
    auto* state = static_cast<State*>(UserSvr::DllTls(reinterpret_cast<TInt>(&_E32Startup), 0xe000096a));
    if (!state) {
        void* memory = User::AllocZ(sizeof(State));
        if (!memory) User::Panic(_L("NgageVideo"), KErrNoMemory);
        state = new(memory) State;
        TInt result = state->video.Load(_L("Z:\\sys\\bin\\mediaclientvideo.dll"));
        if (result == KErrNone) result = state->window.Load(_L("ws32.dll"));
        if (result != KErrNone) User::Panic(_L("NgageVideo"), result);
        result = UserSvr::DllSetTls(reinterpret_cast<TInt>(&_E32Startup), 0xe000096a, state);
        if (result != KErrNone) User::Panic(_L("NgageVideo"), result);
    }
    return *state;
}
void Announce(TInt owner) {
    static _LIT_SECURITY_POLICY_PASS(anyone);
    const TUid category = {static_cast<TInt32>(RProcess().SecureId().iId)};
    RProperty::Define(category, Ngage::kVideoWindowKey, RProperty::EInt, anyone, anyone);
    RProperty::Set(category, Ngage::kVideoWindowKey, owner);
}
template <typename Result, typename... Arguments>
Result Call(State& state, TInt ordinal, TAny* window, Arguments... arguments) {
    return reinterpret_cast<Result (*)(TAny*, Arguments...)>(state.window.Lookup(ordinal))(window, arguments...);
}
bool Displayed(State& state, TAny* window, TSurfaceConfiguration& configuration) {
    TSurfaceId id;
    return Call<TInt, TSurfaceConfiguration*>(state, kGetBackgroundSurface, window, &configuration) == KErrNone &&
           (configuration.GetSurfaceId(id), !id.IsNull());
}
// The player is created on a window without a background surface; the game's picture is put
// back while the file opens, and the player's own surface replaces it.
void TakeWindow(State& state, TAny* window) {
    Announce(static_cast<TInt>(RProcess().Id().Id()));
    state.game = window;
    new(&state.display) TSurfaceConfiguration;
    state.restore = Displayed(state, window, state.display);
    if (state.restore) Call<void, TInt>(state, kRemoveBackgroundSurface, window, ETrue);
}
void RestoreWindow(State& state) {
    if (!state.restore) return;
    state.restore = false;
    Call<TInt, const TSurfaceConfiguration*, TInt>(state, kSetBackgroundSurface, state.game, &state.display, ETrue);
}
// Before the player's surface arrives, the restored picture is taken off again.
void UpdateWindow(State& state, TAny* window) {
    TSurfaceConfiguration current;
    TSurfaceId shown, ours;
    state.display.GetSurfaceId(ours);
    if (window != state.game || !Displayed(state, window, current)) return;
    current.GetSurfaceId(shown);
    if (shown == ours) {
        Call<void, TInt>(state, kRemoveBackgroundSurface, window, ETrue);
        state.restore = true;
    }
}
}
extern "C" TLibraryFunction VideoResolve(TInt ordinal, TInt* args) {
    auto& state = Session();
    // Arguments past r3 follow the saved ip and lr.
    if (ordinal == kNewL) TakeWindow(state, reinterpret_cast<TAny*>(args[7]));
    if (ordinal == kSetDisplayWindowL) UpdateWindow(state, reinterpret_cast<TAny*>(args[3]));
    if (ordinal == kOpenFileL || ordinal == kOpenDesL) RestoreWindow(state);
    if (ordinal == kClose) { Announce(0); state.game = nullptr; state.restore = false; }
    const auto entry = state.video.Lookup(ordinal);
    if (!entry) User::Panic(_L("NgageVideo"), KErrNotFound);
    return entry;
}
