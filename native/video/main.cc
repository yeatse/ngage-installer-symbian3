// MMF video forwarding that keeps the posting surface off the window while a video player needs it.
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
enum { kSetDisplayWindowL = 13, kPlayInterval = 21, kPlay = 22, kClose = 24, kOpenDesL = 27, kOpenFileL = 30,
       kNewL = 20 };
enum { kGetBackgroundSurface = 596, kRemoveBackgroundSurface = 600 };
struct State {
    RLibrary video;
    RLibrary window;
    TAny* game = nullptr;
    TSurfaceId ours = TSurfaceId::CreateNullId();
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
void Announce(bool owned) {
    const TInt process = static_cast<TInt>(RProcess().Id().Id());
    static _LIT_SECURITY_POLICY_PASS(anyone);
    const TUid category = {static_cast<TInt32>(RProcess().SecureId().iId)};
    RProperty::Define(category, Ngage::kVideoWindowKey, RProperty::EInt, anyone, anyone);
    RProperty::Set(category, Ngage::kVideoWindowKey, owned ? process : -process);
}
template <typename Result, typename... Arguments>
Result Call(State& state, TInt ordinal, TAny* window, Arguments... arguments) {
    return reinterpret_cast<Result (*)(TAny*, Arguments...)>(state.window.Lookup(ordinal))(window, arguments...);
}
TSurfaceId Shown(State& state, TAny* window) {
    TSurfaceConfiguration configuration;
    TSurfaceId id = TSurfaceId::CreateNullId();
    if (Call<TInt, TSurfaceConfiguration*>(state, kGetBackgroundSurface, window, &configuration) == KErrNone)
        configuration.GetSurfaceId(id);
    return id;
}
// The 808's player neither starts nor plays on a window showing another background surface, but
// opening and preparing do not need it; the game's frames stay on screen until Play.
void TakeWindow(State& state, TAny* window) {
    Announce(true);
    if (!window || state.ours.IsNull() || Shown(state, window) != state.ours) return;
    Call<void, TInt>(state, kRemoveBackgroundSurface, window, ETrue);
}
void CreatePlayer(State& state, TAny* window) {
    state.game = window;
    state.ours = Shown(state, window);
    TakeWindow(state, window);
}
}
extern "C" TLibraryFunction VideoResolve(TInt ordinal, TInt* args) {
    auto& state = Session();
    // Arguments past r3 follow the saved ip and lr.
    if (ordinal == kNewL) CreatePlayer(state, reinterpret_cast<TAny*>(args[7]));
    if (ordinal == kSetDisplayWindowL) TakeWindow(state, reinterpret_cast<TAny*>(args[3]));
    if (ordinal == kPlay || ordinal == kPlayInterval) TakeWindow(state, state.game);
    if (ordinal == kOpenFileL || ordinal == kOpenDesL) Announce(false);
    if (ordinal == kClose) { Announce(false); state.game = nullptr; state.ours = TSurfaceId::CreateNullId(); }
    const auto entry = state.video.Lookup(ordinal);
    if (!entry) User::Panic(_L("NgageVideo"), KErrNotFound);
    return entry;
}
