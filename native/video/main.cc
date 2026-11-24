// MMF video forwarding that takes the posting surface off the window a video player draws in.
#include <e32std.h>
#include <e32svr.h>
#include <e32property.h>
#include <new>
#include "../common/video_window.h"

CBase::~CBase() {}
void operator delete(void* pointer) noexcept { User::Free(pointer); }
void operator delete(void* pointer, unsigned) noexcept { User::Free(pointer); }
extern "C" void __cxa_pure_virtual() { User::Invariant(); }
extern "C" void _E32Startup();

namespace {
enum { kNewL = 20, kSetDisplayWindowL = 13, kClose = 24, kRemoveBackgroundSurface = 600 };
struct State {
    RLibrary video;
    RLibrary window;
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
// The posting surface stays off while the property names this process.
void TakeWindow(State& state, TAny* window) {
    Announce(static_cast<TInt>(RProcess().Id().Id()));
    using Remove = void (*)(TAny*, TInt);
    const auto remove = reinterpret_cast<Remove>(state.window.Lookup(kRemoveBackgroundSurface));
    if (remove && window) remove(window, ETrue);
}
}
extern "C" TLibraryFunction VideoResolve(TInt ordinal, TInt* args) {
    auto& state = Session();
    // Arguments past r3 follow the saved ip and lr.
    if (ordinal == kNewL) TakeWindow(state, reinterpret_cast<TAny*>(args[7]));
    if (ordinal == kSetDisplayWindowL) TakeWindow(state, reinterpret_cast<TAny*>(args[3]));
    if (ordinal == kClose) Announce(0);
    const auto entry = state.video.Lookup(ordinal);
    if (!entry) User::Panic(_L("NgageVideo"), KErrNotFound);
    return entry;
}
