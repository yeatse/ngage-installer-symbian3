// MMF forwarding with NGI legacy-volume adaptation; the ROM owns playback.
#include <e32std.h>
#include <e32svr.h>
#include <new>
#include "volume.h"
#include "remote_volume.h"

CBase::~CBase() {}
void operator delete(void* pointer) noexcept { User::Free(pointer); }
void operator delete(void* pointer, unsigned) noexcept { User::Free(pointer); }
extern "C" void __cxa_pure_virtual() { User::Invariant(); }
extern "C" void _E32Startup();

namespace {
struct State {
    RLibrary library;
    RLibrary callback_pin;
    TAny* sound = nullptr;
    TInt legacy = 0;
    TInt hardware_level = -1;
    bool remote_attempted = false;
    Ngage::VolumeReceiver* remote = nullptr;
};
void ApplyHardwareVolume(State& state) {
    if (state.sound && state.hardware_level >= 0) {
        using Getter = TInt (*)(TAny*);
        using Setter = void (*)(TAny*, TInt);
        const auto maximum = reinterpret_cast<Getter>(state.library.Lookup(34));
        const auto setter = reinterpret_cast<Setter>(state.library.Lookup(37));
        if (maximum && setter) {
            const TInt limit = maximum(state.sound);
            const TInt applied = Ngage::ScaleLegacyVolume(state.hardware_level, limit);
            setter(state.sound, applied);
        }
    }
}
void ChangeVolume(TAny* context, TInt direction) {
    auto& state = *static_cast<State*>(context);
    TInt level = state.hardware_level < 0 ? state.legacy : state.hardware_level;
    level += direction;
    if (level < 0) level = 0;
    if (level > 10) level = 10;
    state.hardware_level = level;
    ApplyHardwareVolume(state);
}
State& Session() {
    auto* state = static_cast<State*>(UserSvr::DllTls(reinterpret_cast<TInt>(&_E32Startup), 0xe0000964));
    if (!state) {
        void* memory = User::AllocZ(sizeof(State));
        if (!memory) User::Panic(_L("NgageAudio"), KErrNoMemory);
        state = new(memory) State;
        TInt result = state->library.Load(_L("Z:\\sys\\bin\\MmfDevSound.dll"));
        if (result != KErrNone) User::Panic(_L("NgageAudio"), result);
        result = UserSvr::DllSetTls(reinterpret_cast<TInt>(&_E32Startup), 0xe0000964, state);
        if (result != KErrNone) User::Panic(_L("NgageAudio"), result);
    }
    return *state;
}
}
extern "C" TLibraryFunction AudioResolve(TInt ordinal, TInt* args) {
    auto& state = Session();
    if (ordinal == 28 && state.sound == reinterpret_cast<TAny*>(args[0])) state.sound = nullptr;
    if (ordinal == 35 || ordinal == 33) state.sound = reinterpret_cast<TAny*>(args[0]);
    if (ordinal == 35) ApplyHardwareVolume(state);
    if (ordinal == 37) {
        state.sound = reinterpret_cast<TAny*>(args[0]);
        if (args[1] != state.legacy || args[1] == 0) state.hardware_level = -1;
        state.legacy = args[1];
        if (!state.remote_attempted) {
            state.remote_attempted = true;
            const TInt pin = state.callback_pin.Load(_L("C:\\sys\\bin\\NgageSoundX.dll"));
            if (pin == KErrNone) {
                state.remote = Ngage::VolumeReceiver::New(ChangeVolume, &state);
                if (!state.remote) state.callback_pin.Close();
            }
        }
        using Getter = TInt (*)(TAny*);
        const auto getter = reinterpret_cast<Getter>(state.library.Lookup(34));
        if (getter) {
            const TInt maximum = getter(reinterpret_cast<TAny*>(args[0]));
            args[1] = Ngage::ScaleLegacyVolume(state.hardware_level < 0 ? args[1] : state.hardware_level, maximum);
        }
    }
    const auto entry = state.library.Lookup(ordinal);
    if (!entry) User::Panic(_L("NgageAudio"), KErrNotFound);
    return entry;
}
