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

}  // namespace
void WrapVtable(TAny* object, TInt slots);
namespace {
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
    if (result == KErrNone && out && *out && real) {
        // RG_fileaccess layout: ordinal 1 at code+0x4d9, primary vtable at code+0x4a9c.
        const TUint code = reinterpret_cast<TUint>(real) - 0x4d9;
        TUint* object = static_cast<TUint*>(*out);
        line.AppendFormat(_L8(" vptr=%x vptr2=%x"), object[0] - code, object[1] - code);
        if (object[0] == code + 0x4a9c) {
            WrapVtable(object, 30);
            line.Append(_L8(" wrapped"));
        }
        if (object[1] == code + 0x4a9c + 0x80) WrapVtable(object + 1, 2);
    }
    line.Commit();
    return result;
}

}  // namespace

namespace {
const TInt KShadowDepth = 32;
const TInt KTlsKey = 0xe00009a0;
struct Shadow {
    TInt depth;
    TInt logged;
    TUint lr[KShadowDepth];
    TInt ordinal[KShadowDepth];  // factory ordinal, or 100 + vtable slot
    TUint r2[KShadowDepth];
};
const TInt KVerboseLines = 1500;
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

// Dumps 64 bytes at a register value and at the word it points to, when readable.
void DumpBytes(TDes8& line, TUint address) {
    const TUint8* bytes = reinterpret_cast<const TUint8*>(address);
    for (TInt i = 0; i < 64; ++i) line.AppendFormat(_L8("%02x"), bytes[i]);
    line.Append(' ');
    for (TInt i = 0; i < 64; ++i) line.Append(bytes[i] >= 32 && bytes[i] < 127 ? bytes[i] : '.');
}

// Only the current thread's stack and heap are known to be mapped.
bool Readable(TUint address) {
    const TUint end = address + 64;
    TThreadStackInfo stack;
    if (RThread().StackInfo(stack) == KErrNone && address >= stack.iLimit && end <= stack.iBase)
        return true;
    RHeap& heap = User::Heap();
    const TUint base = reinterpret_cast<TUint>(heap.Base());
    return address >= base && end <= base + static_cast<TUint>(heap.Size());
}

void DumpPointer(TInt ordinal, TInt index, TUint address) {
    if (!Readable(address)) return;
    Line line;
    line.AppendFormat(_L8("  f%d r%d @%08x "), ordinal, index, address);
    DumpBytes(line, address);
    line.Commit();
    const TUint target = *reinterpret_cast<const TUint*>(address);
    if (!Readable(target)) return;
    Line deref;
    deref.AppendFormat(_L8("  f%d *r%d @%08x "), ordinal, index, target);
    DumpBytes(deref, target);
    deref.Commit();
}

extern "C" {
void VtableThunk0();
void VtableThunk1();
void VtableThunk2();
void VtableThunk3();
void VtableThunk4();
void VtableThunk5();
void VtableThunk6();
void VtableThunk7();
void VtableThunk8();
void VtableThunk9();
void VtableThunk10();
void VtableThunk11();
void VtableThunk12();
void VtableThunk13();
void VtableThunk14();
void VtableThunk15();
void VtableThunk16();
void VtableThunk17();
void VtableThunk18();
void VtableThunk19();
void VtableThunk20();
void VtableThunk21();
void VtableThunk22();
void VtableThunk23();
void VtableThunk24();
void VtableThunk25();
void VtableThunk26();
void VtableThunk27();
void VtableThunk28();
void VtableThunk29();
}
void (*const VtableThunks[30])() = {
    VtableThunk0,
    VtableThunk1,
    VtableThunk2,
    VtableThunk3,
    VtableThunk4,
    VtableThunk5,
    VtableThunk6,
    VtableThunk7,
    VtableThunk8,
    VtableThunk9,
    VtableThunk10,
    VtableThunk11,
    VtableThunk12,
    VtableThunk13,
    VtableThunk14,
    VtableThunk15,
    VtableThunk16,
    VtableThunk17,
    VtableThunk18,
    VtableThunk19,
    VtableThunk20,
    VtableThunk21,
    VtableThunk22,
    VtableThunk23,
    VtableThunk24,
    VtableThunk25,
    VtableThunk26,
    VtableThunk27,
    VtableThunk28,
    VtableThunk29,
};

// Wrapped vtable copy: [original vtable][offset-to-top][typeinfo][slots...].
void WrapVtable(TAny* object, TInt slots) {
    TUint** vptr = static_cast<TUint**>(object);
    TUint* original = *vptr;
    TUint* copy = static_cast<TUint*>(User::Alloc((slots + 3) * sizeof(TUint)));
    if (!copy) return;
    copy[0] = reinterpret_cast<TUint>(original);
    copy[1] = original[-2];
    copy[2] = original[-1];
    for (TInt i = 0; i < slots; ++i) copy[3 + i] = reinterpret_cast<TUint>(VtableThunks[i]);
    *vptr = copy + 3;
}

extern "C" __attribute__((visibility("default"))) TAny* VtableResolve(TInt slot, TUint* args) {
    TUint* vptr = *reinterpret_cast<TUint**>(args[0]);
    TUint* original = reinterpret_cast<TUint*>(vptr[-3]);
    Shadow* shadow = ThreadShadow();
    if (shadow->logged < KVerboseLines) {
        ++shadow->logged;
        Line line;
        line.AppendFormat(_L8("fa.v%d enter this=%08x r1=%08x r2=%08x r3=%08x caller=%x"), slot, args[0],
                          args[1], args[2], args[3], ExeOffset(reinterpret_cast<TAny*>(args[5])));
        line.Commit();
        for (TInt i = 1; i < 4; ++i) DumpPointer(200 + slot, i, args[i]);
    }
    return reinterpret_cast<TAny*>(original[slot]);
}

extern "C" __attribute__((visibility("default"))) TAny* VtableResolveReturn(TInt slot, TUint* args) {
    Shadow* shadow = ThreadShadow();
    if (shadow->depth >= KShadowDepth) User::Panic(_L("RG_dl diag"), KErrOverflow);
    shadow->lr[shadow->depth] = args[5];
    shadow->ordinal[shadow->depth] = 100 + slot;
    shadow->r2[shadow->depth] = args[2];
    ++shadow->depth;
    return VtableResolve(slot, args);
}

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
    if (ordinal == 1 || ordinal == 5)
        for (TInt i = 0; i < 4; ++i) DumpPointer(ordinal, i, args[i]);
    return Export(KFactory, ordinal);
}

extern "C" __attribute__((visibility("default"))) TUint FactoryLeave(TUint r0, TUint r1) {
    Shadow* shadow = ThreadShadow();
    --shadow->depth;
    const TInt id = shadow->ordinal[shadow->depth];
    if (id >= 100) {
        if (shadow->logged < KVerboseLines || r0 != 0) {
            ++shadow->logged;
            Line line;
            line.AppendFormat(_L8("fa.v%d return r0=%08x r1=%08x"), id - 100, r0, r1);
            line.Commit();
            DumpPointer(300 + id - 100, 2, shadow->r2[shadow->depth]);
        }
    } else if (shadow->logged < KVerboseLines || (r0 != 0 && r0 != 1 && r0 != 0x20)) {
        ++shadow->logged;
        Line line;
        line.AppendFormat(_L8("factory.%d return r0=%08x r1=%08x"), id, r0, r1);
        line.Commit();
    }
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
