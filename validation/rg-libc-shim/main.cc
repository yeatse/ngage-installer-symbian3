// Diagnostic libc stand-in for Reset Generation's file-manager factory, as
// Hooked's BiNPDA shim does: memcpy/memmove/memset go to euser, every other
// ordinal tail-calls the ROM libc export with the same ordinal. strcpy uses
// stpcpy, as the BiNPDA shim does.
#include <e32std.h>
#include <e32svr.h>

CBase::~CBase() {}
void operator delete(void* pointer) noexcept { User::Free(pointer); }
void operator delete(void* pointer, unsigned) noexcept { User::Free(pointer); }
extern "C" void __cxa_pure_virtual() { User::Invariant(); }

extern "C" {
TAny* memcpy(TAny*, const TAny*, unsigned int);
TAny* memmove(TAny*, const TAny*, unsigned int);
TAny* memset(TAny*, int, unsigned int);
}

namespace {
const TInt KTlsKey = 0xe00009a1;
struct Libc {
    RLibrary library;
};
}

extern "C" __attribute__((visibility("default"))) TAny* LibcResolve(TInt ordinal) {
    auto* libc = static_cast<Libc*>(UserSvr::DllTls(KTlsKey, KTlsKey));
    if (!libc) {
        libc = static_cast<Libc*>(User::AllocZ(sizeof(Libc)));
        if (!libc) User::Panic(_L("rgc"), KErrNoMemory);
        new (&libc->library) RLibrary;
        TInt r = libc->library.Load(_L("libc.dll"));
        if (r != KErrNone) User::Panic(_L("rgc"), r);
        r = UserSvr::DllSetTls(KTlsKey, KTlsKey, libc);
        if (r != KErrNone) User::Panic(_L("rgc"), r);
    }
    TLibraryFunction function = libc->library.Lookup(ordinal);
    if (!function) User::Panic(_L("rgc"), ordinal);
    return reinterpret_cast<TAny*>(function);
}

// Hooked's shim routes strcpy to libc's stpcpy (ordinal 345) and returns its result.
extern "C" __attribute__((visibility("default"))) char* ShimStrcpy(char* to, const char* from) {
    using Stpcpy = char* (*)(char*, const char*);
    return reinterpret_cast<Stpcpy>(LibcResolve(345))(to, from);
}

extern "C" __attribute__((visibility("default"))) TAny* ShimMemcpy(TAny* to, const TAny* from, unsigned int size) {
    return memcpy(to, from, size);
}
extern "C" __attribute__((visibility("default"))) TAny* ShimMemmove(TAny* to, const TAny* from, unsigned int size) {
    return memmove(to, from, size);
}
extern "C" __attribute__((visibility("default"))) TAny* ShimMemset(TAny* to, int value, unsigned int size) {
    return memset(to, value, size);
}
