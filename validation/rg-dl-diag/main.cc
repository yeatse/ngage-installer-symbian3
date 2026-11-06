// Diagnostic stand-in for Reset Generation's RG_dl.dll. Same exports and
// ngifileaccess.dll -> RG_fileaccess.dll redirect, plus a call log in
// C:\Data\rg_dl.log. Never shipped; for one phone run only.
#include <e32std.h>
#include <f32file.h>
#include <e32svr.h>

CBase::~CBase() {}
void operator delete(void* pointer) noexcept { User::Free(pointer); }
void operator delete(void* pointer, unsigned) noexcept { User::Free(pointer); }
extern "C" void __cxa_pure_virtual() { User::Invariant(); }

extern "C" {
void* dlopen(const char*, int);
void* dlsym(void*, const char*);
int dlclose(void*);
char* dlerror();
int strcmp(const char*, const char*);
void FactoryThunk1();
void FactoryThunk2();
void FactoryThunk3();
void FactoryThunk4();
void FactoryThunk5();
void FactoryThunk6();
}

namespace {

_LIT(KAccess, "RG_fileaccess.dll");
_LIT(KFactory, "ngifilemanagerfactory_2000AFC4.dll");

TUint ExeOffset(TAny* address) {
    TProcessMemoryInfo info;
    if (RProcess().GetMemoryInfo(info) != KErrNone) return 0;
    return reinterpret_cast<TUint>(address) - info.iCodeBase + 0x8000;
}

void Write(const TDesC8& line) {
    RFs fs;
    if (fs.Connect() != KErrNone) return;
    fs.MkDirAll(_L("C:\\Data\\"));
    RFile file;
    TInt r = file.Open(fs, _L("C:\\Data\\rg_dl.log"), EFileWrite | EFileShareAny);
    if (r == KErrNotFound) r = file.Create(fs, _L("C:\\Data\\rg_dl.log"), EFileWrite | EFileShareAny);
    if (r == KErrNone) {
        TInt end = 0;
        file.Seek(ESeekEnd, end);
        file.Write(line);
        file.Close();
    }
    fs.Close();
}

class Line : public TBuf8<400> {
public:
    Line() { AppendFormat(_L8("%u t%u "), User::NTickCount(), (TUint)RThread().Id()); }
    void Commit() {
        Append('\n');
        Write(*this);
    }
};

void AppendC(TDes8& out, const char* text) {
    if (!text) {
        out.Append(_L8("(null)"));
        return;
    }
    TPtrC8 value(reinterpret_cast<const TUint8*>(text));
    out.Append(value.Left(Min(value.Length(), out.MaxLength() - out.Length() - 2)));
}

TAny* Export(const TDesC& name, TInt ordinal) {
    RLibrary library;
    if (library.Load(name) != KErrNone) return nullptr;
    TAny* address = reinterpret_cast<TAny*>(library.Lookup(ordinal));
    library.Close();  // dlopen keeps the library loaded.
    return address;
}

typedef TInt (*CreateInstance)(TInt, TAny**);

TInt WrappedAccessCreate(TInt id, TAny** out) {
    {
        Line line;
        line.AppendFormat(_L8("fileaccess.1 enter id=%x out=%08x caller=%x"), id, (TUint)out,
                          ExeOffset(__builtin_return_address(0)));
        line.Commit();
    }
    const auto real = reinterpret_cast<CreateInstance>(Export(KAccess, 1));
    const TInt result = real ? real(id, out) : KErrNotFound;
    Line line;
    line.AppendFormat(_L8("fileaccess.1 return r=%d object=%08x"), result, out ? (TUint)*out : 0);
    line.Commit();
    return result;
}

}  // namespace

namespace {
const TInt KShadowDepth = 32;
const TInt KTlsKey = 0xe00009a0;
struct Shadow {
    TInt depth;
    TUint lr[KShadowDepth];
    TInt ordinal[KShadowDepth];
};
Shadow* ThreadShadow() {
    auto* shadow = static_cast<Shadow*>(UserSvr::DllTls(KTlsKey, KTlsKey));
    if (!shadow) {
        shadow = static_cast<Shadow*>(User::AllocZ(sizeof(Shadow)));
        if (!shadow || UserSvr::DllSetTls(KTlsKey, KTlsKey, shadow) != KErrNone)
            User::Panic(_L("RG_dl diag"), KErrNoMemory);
    }
    return shadow;
}
}  // namespace

// args: saved r0-r3, ip, lr of the thunk's caller.
extern "C" __attribute__((visibility("default"))) TAny* FactoryResolve(TInt ordinal, TUint* args) {
    Shadow* shadow = ThreadShadow();
    if (shadow->depth >= KShadowDepth) User::Panic(_L("RG_dl diag"), KErrOverflow);
    shadow->lr[shadow->depth] = args[5];
    shadow->ordinal[shadow->depth] = ordinal;
    ++shadow->depth;
    Line line;
    line.AppendFormat(_L8("factory.%d enter r0=%08x r1=%08x r2=%08x r3=%08x caller=%x"), ordinal,
                      args[0], args[1], args[2], args[3], ExeOffset(reinterpret_cast<TAny*>(args[5])));
    line.Commit();
    return Export(KFactory, ordinal);
}

extern "C" __attribute__((visibility("default"))) TUint FactoryLeave(TUint r0, TUint r1) {
    Shadow* shadow = ThreadShadow();
    --shadow->depth;
    Line line;
    line.AppendFormat(_L8("factory.%d return r0=%08x r1=%08x"), shadow->ordinal[shadow->depth], r0, r1);
    line.Commit();
    return shadow->lr[shadow->depth];
}

extern "C" __attribute__((visibility("default"))) int DiagDlclose(void* handle) {
    const int result = dlclose(handle);
    Line line;
    line.AppendFormat(_L8("dlclose %08x r=%d caller=%x"), (TUint)handle, result,
                      ExeOffset(__builtin_return_address(0)));
    line.Commit();
    return result;
}

extern "C" __attribute__((visibility("default"))) int DiagUnused() { return 0; }

extern "C" __attribute__((visibility("default"))) void* DiagDlopen(const char* name, int mode) {
    const char* target = name;
    if (name && strcmp(name, "ngifileaccess.dll") == 0) target = "RG_fileaccess.dll";
    void* handle = dlopen(target, mode);
    Line line;
    line.Append(_L8("dlopen "));
    AppendC(line, name);
    line.Append(_L8(" -> "));
    AppendC(line, target);
    line.AppendFormat(_L8(" mode=%d handle=%08x caller=%x"), mode, (TUint)handle,
                      ExeOffset(__builtin_return_address(0)));
    if (!handle) {
        line.Append(_L8(" error="));
        AppendC(line, dlerror());
    }
    line.Commit();
    return handle;
}

extern "C" __attribute__((visibility("default"))) void* DiagDlsym(void* handle, const char* name) {
    void* symbol = dlsym(handle, name);
    Line line;
    line.AppendFormat(_L8("dlsym %08x "), (TUint)handle);
    AppendC(line, name);
    line.AppendFormat(_L8(" -> %08x caller=%x"), (TUint)symbol, ExeOffset(__builtin_return_address(0)));
    if (!symbol) {
        line.Append(_L8(" error="));
        AppendC(line, dlerror());
    }
    static void (*const kThunks[])() = { FactoryThunk1, FactoryThunk2, FactoryThunk3,
                                         FactoryThunk4, FactoryThunk5, FactoryThunk6 };
    if (symbol && name && name[0] >= '1' && name[0] <= '6' && name[1] == 0) {
        const TInt ordinal = name[0] - '0';
        if (ordinal == 1 && symbol == Export(KAccess, 1)) {
            symbol = reinterpret_cast<void*>(&WrappedAccessCreate);
            line.Append(_L8(" wrapped"));
        } else if (symbol == Export(KFactory, ordinal)) {
            symbol = reinterpret_cast<void*>(kThunks[ordinal - 1]);
            line.Append(_L8(" wrapped"));
        }
    }
    line.Commit();
    return symbol;
}
