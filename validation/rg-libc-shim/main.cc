// Diagnostic libc stand-in for Reset Generation's file-manager factory.
// memcpy/memmove/memset go to euser, as Hooked's BiNPDA shim does. Every
// other ordinal tail-calls the ROM libc export with the same ordinal; the
// ones the factory imports are logged to C:\Data\rg_dl.log with their result.
#include <e32std.h>
#include <e32svr.h>
#include <f32file.h>

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
const TInt KShadowDepth = 32;
const TInt KMaxLines = 20000;

struct State {
    RLibrary library;
    RLibrary factory;
    TUint factoryCode;
    TInt depth;
    TInt lines;
    TUint lr[KShadowDepth];
    TInt ordinal[KShadowDepth];
    TUint args[KShadowDepth][4];
};

State& ThreadState() {
    auto* state = static_cast<State*>(UserSvr::DllTls(KTlsKey, KTlsKey));
    if (!state) {
        state = static_cast<State*>(User::AllocZ(sizeof(State)));
        if (!state) User::Panic(_L("rgc"), KErrNoMemory);
        new (&state->library) RLibrary;
        new (&state->factory) RLibrary;
        TInt r = state->library.Load(_L("libc.dll"));
        if (r != KErrNone) User::Panic(_L("rgc"), r);
        if (state->factory.Load(_L("ngifilemanagerfactory_2000AFC4.dll")) == KErrNone)
            state->factoryCode = reinterpret_cast<TUint>(state->factory.Lookup(1)) & ~0xfffu;
        r = UserSvr::DllSetTls(KTlsKey, KTlsKey, state);
        if (r != KErrNone) User::Panic(_L("rgc"), r);
    }
    return *state;
}

const char* Name(TInt ordinal) {
    switch (ordinal) {
    case 58: return "closedir"; case 75: return "fclose"; case 81: return "fflush";
    case 90: return "fopen"; case 98: return "free"; case 113: return "fwrite";
    case 216: return "localtime"; case 219: return "lseek"; case 221: return "malloc";
    case 252: return "opendir"; case 271: return "random"; case 272: return "read";
    case 273: return "readdir"; case 276: return "realloc"; case 342: return "srandom";
    case 346: return "strcasecmp"; case 348: return "strcat"; case 352: return "strcpy";
    case 358: return "strftime"; case 361: return "strlen"; case 384: return "strtoul";
    case 397: return "time"; default: return "?";
    }
}

bool Readable(State& state, TUint address, TUint length) {
    const TUint end = address + length;
    if (end < address) return false;
    TThreadStackInfo stack;
    if (RThread().StackInfo(stack) == KErrNone && address >= stack.iLimit && end <= stack.iBase) return true;
    RHeap& heap = User::Heap();
    const TUint base = reinterpret_cast<TUint>(heap.Base());
    if (address >= base && end <= base + static_cast<TUint>(heap.Size())) return true;
    return state.factoryCode && address >= state.factoryCode && end <= state.factoryCode + 0xb9000;
}

void Write(const TDesC8& line) {
    RFs fs;
    if (fs.Connect() != KErrNone) return;
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

void AppendString(State& state, TDes8& line, TUint address) {
    if (!Readable(state, address, 1)) {
        line.AppendFormat(_L8("%08x"), address);
        return;
    }
    line.Append('"');
    const TUint8* text = reinterpret_cast<const TUint8*>(address);
    for (TInt i = 0; i < 120 && Readable(state, address + i, 1) && text[i]; ++i)
        line.Append(text[i] >= 32 && text[i] < 127 ? text[i] : '.');
    line.Append('"');
}

void AppendBytes(State& state, TDes8& line, TUint address, TInt count) {
    if (!Readable(state, address, count)) {
        line.AppendFormat(_L8("%08x"), address);
        return;
    }
    const TUint8* bytes = reinterpret_cast<const TUint8*>(address);
    for (TInt i = 0; i < count; ++i) line.AppendFormat(_L8("%02x"), bytes[i]);
}
}  // namespace

extern "C" __attribute__((visibility("default"))) TAny* LibcResolve(TInt ordinal) {
    TLibraryFunction function = ThreadState().library.Lookup(ordinal);
    if (!function) User::Panic(_L("rgc"), ordinal);
    return reinterpret_cast<TAny*>(function);
}

// args: the caller's r0-r3, ip, lr.
extern "C" __attribute__((visibility("default"))) TAny* LibcEnter(TInt ordinal, TUint* args) {
    State& state = ThreadState();
    if (state.depth >= KShadowDepth) User::Panic(_L("rgc"), KErrOverflow);
    state.lr[state.depth] = args[5];
    state.ordinal[state.depth] = ordinal;
    for (TInt i = 0; i < 4; ++i) state.args[state.depth][i] = args[i];
    ++state.depth;
    return LibcResolve(ordinal);
}

extern "C" __attribute__((visibility("default"))) TUint LibcLeave(TUint result) {
    State& state = ThreadState();
    --state.depth;
    const TInt ordinal = state.ordinal[state.depth];
    const TUint* a = state.args[state.depth];
    const bool quiet = ordinal == 361 || ordinal == 98 || ordinal == 221 || ordinal == 276 || ordinal == 271;
    if (state.lines < KMaxLines && (!quiet || (ordinal != 98 && result == 0))) {
        ++state.lines;
        TBuf8<512> line;
        line.AppendFormat(_L8("%u t%u   libc.%s("), User::NTickCount(), (TUint)RThread().Id(), Name(ordinal));
        switch (ordinal) {
        case 90: case 346: case 348:
            AppendString(state, line, a[0]); line.Append(','); AppendString(state, line, a[1]); break;
        case 352:
            line.AppendFormat(_L8("%08x,"), a[0]); AppendString(state, line, a[1]); break;
        case 252: case 384:
            AppendString(state, line, a[0]); break;
        case 358:
            line.AppendFormat(_L8("%08x,%d,"), a[0], a[1]); AppendString(state, line, a[2]); break;
        default:
            line.AppendFormat(_L8("%x,%x,%x,%x"), a[0], a[1], a[2], a[3]);
        }
        line.AppendFormat(_L8(") = %x"), result);
        if (ordinal == 358 || ordinal == 352 || ordinal == 348) {
            line.Append(_L8(" -> "));
            AppendString(state, line, a[0]);
        } else if (ordinal == 273 && result) {
            line.Append(_L8(" dirent="));
            AppendBytes(state, line, result, 8);
            line.Append(' ');
            AppendString(state, line, result + 8);
        } else if (ordinal == 272 && static_cast<TInt>(result) > 0) {
            line.Append(_L8(" data="));
            AppendBytes(state, line, a[1], Min<TInt>(static_cast<TInt>(result), 32));
        }
        line.Append(_L8(" caller="));
        line.AppendFormat(_L8("%x"), state.lr[state.depth] - state.factoryCode);
        line.Append('\n');
        Write(line);
    }
    return state.lr[state.depth];
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
