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
// Instances are only compared by address: games may delete DevSound through its
// virtual destructor, so a remembered instance is never dereferenced.
struct Applied { TAny* sound; TUint generation; };
struct State {
    RLibrary library;
    RLibrary callback_pin;
    TInt legacy = 0;
    TInt hardware_level = -1;
    TUint generation = 0;
    TUint next_slot = 0;
    Applied applied[8] = {};
    bool remote_attempted = false;
    Ngage::VolumeReceiver* remote = nullptr;
};
Applied* Find(State& state, TAny* sound) {
    for (auto& entry : state.applied) if (entry.sound == sound) return &entry;
    auto& entry = state.applied[state.next_slot++ % 8];
    entry = { sound, state.generation - 1 };
    return &entry;
}
TInt DeviceVolume(State& state, TAny* sound, TInt legacy) {
    using Getter = TInt (*)(TAny*);
    const auto maximum = reinterpret_cast<Getter>(state.library.Lookup(34));
    return maximum ? Ngage::ScaleLegacyVolume(legacy, maximum(sound)) : legacy;
}
// Runs while the game is calling into this instance, so it is alive.
void ApplyHardwareVolume(State& state, TAny* sound) {
    Applied* entry = Find(state, sound);
    if (state.hardware_level < 0 || entry->generation == state.generation) return;
    entry->generation = state.generation;
    using Setter = void (*)(TAny*, TInt);
    const auto setter = reinterpret_cast<Setter>(state.library.Lookup(37));
    if (setter) setter(sound, DeviceVolume(state, sound, state.hardware_level));
}
void ChangeVolume(TAny* context, TInt direction) {
    auto& state = *static_cast<State*>(context);
    TInt level = state.hardware_level < 0 ? state.legacy : state.hardware_level;
    level += direction;
    if (level < 0) level = 0;
    if (level > 10) level = 10;
    state.hardware_level = level;
    ++state.generation;
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
    auto* sound = reinterpret_cast<TAny*>(args[0]);
    if (ordinal == 33 || ordinal == 35) ApplyHardwareVolume(state, sound);
    if (ordinal == 37) {
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
        Find(state, sound)->generation = state.generation;
        args[1] = DeviceVolume(state, sound, state.hardware_level < 0 ? args[1] : state.hardware_level);
    }
    const auto entry = state.library.Lookup(ordinal);
    if (!entry) User::Panic(_L("NgageAudio"), KErrNotFound);
    return entry;
}
