// MMF forwarding with NGI legacy-volume adaptation; the ROM owns playback.
#include <e32std.h>
#include <e32svr.h>
#include <new>
#include "../common/trace.h"
#include "volume.h"

extern "C" void _E32Startup();

namespace {
struct State {
    RLibrary library;
    TUint calls[45]{};
};
State& Session() {
    auto* state = static_cast<State*>(UserSvr::DllTls(reinterpret_cast<TInt>(&_E32Startup), 0xe0000964));
    if (!state) {
        void* memory = User::AllocZ(sizeof(State));
        if (!memory) User::Panic(_L("NgageAudio"), KErrNoMemory);
        state = new(memory) State;
        TInt result = state->library.Load(_L("Z:\\sys\\bin\\MmfDevSound.dll"));
        Ngage::Trace("audio", "rom.load", result, 1548);
        if (result != KErrNone) User::Panic(_L("NgageAudio"), result);
        result = UserSvr::DllSetTls(reinterpret_cast<TInt>(&_E32Startup), 0xe0000964, state);
        if (result != KErrNone) User::Panic(_L("NgageAudio"), result);
    }
    return *state;
}
}
extern "C" TLibraryFunction AudioResolve(TInt ordinal, TInt* args) {
    auto& state = Session();
    if (ordinal == 37) {
        using Getter = TInt (*)(TAny*);
        const auto getter = reinterpret_cast<Getter>(state.library.Lookup(34));
        if (getter) {
            const TInt requested = args[1];
            const TInt maximum = getter(reinterpret_cast<TAny*>(args[0]));
            args[1] = Ngage::ScaleLegacyVolume(requested, maximum);
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
