// MMF forwarding with NGI legacy-volume adaptation; the ROM owns playback.
#include <e32std.h>
#include <e32svr.h>
#include <new>
#include "../common/trace.h"
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
    TUint calls[45]{};
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
            Ngage::Trace("audio", "volume.hardware.applied.max", state.hardware_level, applied, limit);
        }
    } else Ngage::Trace("audio", "volume.hardware.paused", state.hardware_level);
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
        Ngage::Trace("audio", "rom.load", result, 1549);
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
            Ngage::Trace("audio", "remote.module.pin", pin);
            if (pin == KErrNone) {
                state.remote = Ngage::VolumeReceiver::New(ChangeVolume, &state);
                if (!state.remote) state.callback_pin.Close();
            }
        }
        using Getter = TInt (*)(TAny*);
        const auto getter = reinterpret_cast<Getter>(state.library.Lookup(34));
        if (getter) {
            const TInt requested = args[1];
            const TInt maximum = getter(reinterpret_cast<TAny*>(args[0]));
            args[1] = Ngage::ScaleLegacyVolume(state.hardware_level < 0 ? requested : state.hardware_level, maximum);
            Ngage::Trace("audio", "volume.legacy.applied.max", requested, args[1], maximum);
        }
    }
#if NGAGE_DIAGNOSTIC
    const TUint count = ++state.calls[ordinal];
    const bool record = ordinal != 33 || count <= 3 || (count & 255) == 0;
    if (record) {
        Ngage::Trace("audio", "call", ordinal, count, args[0]);
        switch (ordinal) {
        case 3: {
            const auto* config = reinterpret_cast<const TInt*>(args[1]);
            Ngage::Trace("audio", "config.rate.encoding.channels", config[0], config[1], config[2]);
            Ngage::Trace("audio", "config.buffer", config[3]);
            break;
        }
        case 5: Ngage::Trace("audio", "initialize.fourcc.state", args[2], args[3]); break;
        case 6: Ngage::Trace("audio", "initialize.state", args[2]); break;
        case 18: Ngage::Trace("audio", "balance", args[1], args[2]); break;
        case 32: Ngage::Trace("audio", "gain", args[1]); break;
        case 37: {
            using Getter = TInt (*)(TAny*);
            const auto max_volume = reinterpret_cast<Getter>(state.library.Lookup(34));
            const auto volume = reinterpret_cast<Getter>(state.library.Lookup(30));
            Ngage::Trace("audio", "volume.request.max.current", args[1],
                         max_volume ? max_volume(reinterpret_cast<TAny*>(args[0])) : KErrNotSupported,
                         volume ? volume(reinterpret_cast<TAny*>(args[0])) : KErrNotSupported);
            break;
        }
        }
    }
#endif
    const auto entry = state.library.Lookup(ordinal);
    if (!entry) User::Panic(_L("NgageAudio"), KErrNotFound);
    return entry;
}
