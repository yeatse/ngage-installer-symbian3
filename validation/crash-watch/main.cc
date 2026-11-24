#include <e32std.h>
#include <e32hal.h>
#include <f32file.h>

// Records exits and register contexts of every non-ROM process started after this
// watcher, plus the RAM-loaded libraries they use, in C:\Data\ngage_crash.txt.
namespace {
_LIT(KLogFile, "C:\\Data\\ngage_crash.txt");
_LIT(KMutex, "NgageCrashWatch");
const TInt kBuild = 2;
const TInt kPollMicroseconds = 50000;
const TInt kTrail = 16;
const TInt kIdleLimitPolls = 20 * 60 * 20;

RFs gFs;

void Line(const TDesC8& text) {
    RFile file;
    TInt result = file.Open(gFs, KLogFile, EFileWrite | EFileShareAny);
    if (result == KErrNotFound) result = file.Create(gFs, KLogFile, EFileWrite | EFileShareAny);
    if (result != KErrNone) return;
    TInt offset = 0;
    if (file.Seek(ESeekEnd, offset) == KErrNone) {
        TBuf8<24> stamp;
        stamp.AppendNum(User::NTickCount());
        stamp.Append(' ');
        file.Write(stamp);
        file.Write(text);
        file.Write(_L8("\r\n"));
        file.Flush();
    }
    file.Close();
}

void Append(TDes8& out, const TDesC& text) {
    for (TInt i = 0; i < text.Length() && out.Length() < out.MaxLength(); ++i)
        out.Append(text[i] < 128 ? static_cast<TUint8>(text[i]) : '?');
}

#define LOG(...) do { TBuf8<512> line_; line_.AppendFormat(__VA_ARGS__); Line(line_); } while (0)

void LogWithName(const TDesC8& prefix, const TDesC& name) {
    TBuf8<512> line(prefix);
    line.Append(' ');
    Append(line, name);
    Line(line);
}

void LogRam(const char* when) {
    TMemoryInfoV1Buf info;
    if (UserHal::MemoryInfo(info) == KErrNone)
        LOG(_L8("ram %s free=%d total=%d"), when, info().iFreeRamInBytes, info().iTotalRamInBytes);
}

// RAM-loaded libraries stay open here so their code stays at the address the game used.
struct Library {
    RLibrary handle;
    TUint first = 0;
    TUint last = 0;
    TBuf<64> name;
};
Library gLibraries[384];
TInt gLibraryCount = 0;

// Names of libraries already considered, so each one is loaded and checked once.
TUint32 gSeenLibraries[2048];
TInt gSeenLibraryCount = 0;

TUint32 Hash(const TDesC& text) {
    TUint32 hash = 2166136261u;
    for (TInt i = 0; i < text.Length(); ++i) hash = (hash ^ TChar(text[i]).GetLowerCase()) * 16777619u;
    return hash;
}

TPtrC LibraryFile(const TDesC& found, TFileName& file) {
    file = found.Mid(found.LocateReverse(':') + 1);
    TInt cut = file.Locate('{');
    if (cut >= 0) file.SetLength(cut);
    cut = file.Locate('[');
    if (cut >= 0) file.SetLength(cut);
    if (file.Locate('.') < 0) file.Append(_L(".dll"));
    return file;
}

void RecordLibraries() {
    TFindLibrary find(_L("*"));
    TFullName found;
    while (find.Next(found) == KErrNone && gLibraryCount < static_cast<TInt>(sizeof(gLibraries) / sizeof(gLibraries[0]))) {
        TFileName file;
        LibraryFile(found, file);
        const TUint32 hash = Hash(file);
        bool known = false;
        for (TInt i = 0; i < gSeenLibraryCount && !known; ++i) known = gSeenLibraries[i] == hash;
        if (known) continue;
        if (gSeenLibraryCount < static_cast<TInt>(sizeof(gSeenLibraries) / sizeof(gSeenLibraries[0])))
            gSeenLibraries[gSeenLibraryCount++] = hash;
        Library& library = gLibraries[gLibraryCount];
        if (library.handle.Load(file) != KErrNone) continue;
        const TFileName path = library.handle.FileName();
        if (path.Length() && (path[0] == 'Z' || path[0] == 'z')) {
            library.handle.Close();
            continue;
        }
        library.name = file.Left(64);
        TInt ordinal = 1;
        for (; ordinal < 5000; ++ordinal) {
            const TUint address = reinterpret_cast<TUint>(library.handle.Lookup(ordinal)) & ~1u;
            if (!address) break;
            if (!library.first || address < library.first) library.first = address;
            if (address > library.last) library.last = address;
        }
        TInt code = 0, data = 0;
        library.handle.GetRamSizes(code, data);
        TBuf8<512> line;
        line.AppendFormat(_L8("lib ord1=%08x exports=%d min=%08x max=%08x code=%d const=%d "),
                          reinterpret_cast<TUint>(library.handle.Lookup(1)), ordinal - 1,
                          library.first, library.last, code, data);
        Append(line, path);
        Line(line);
        ++gLibraryCount;
    }
}

// ROM and RAM library code is mapped at the same address in every process, so the
// nearest export below an address names the function it points into.
const TInt kMapped = 40;
TBuf<48> gBestFile[kMapped];
void MapAddresses(const TUint32* regs, TInt words, const TDesC8& label) {
    TUint best[kMapped] = {};
    TInt best_ordinal[kMapped] = {};
    TBuf<48>* best_file = gBestFile;
    TFindLibrary find(_L("*"));
    TFullName found;
    TInt count = 0;
    while (find.Next(found) == KErrNone && count < 400) {
        ++count;
        TFileName file;
        LibraryFile(found, file);
        RLibrary library;
        if (library.Load(file) != KErrNone) continue;
        for (TInt ordinal = 1; ordinal < 5000; ++ordinal) {
            const TUint address = reinterpret_cast<TUint>(library.Lookup(ordinal)) & ~1u;
            if (!address) break;
            for (TInt r = 0; r < words && r < kMapped; ++r)
                if (address <= regs[r] && address > best[r] && regs[r] - address < 0x100000) {
                    best[r] = address;
                    best_ordinal[r] = ordinal;
                    best_file[r] = file.Left(48);
                }
        }
        library.Close();
    }
    for (TInt r = 0; r < words && r < kMapped; ++r) {
        if (!best[r]) continue;
        TBuf8<160> line;
        line.AppendFormat(_L8("near %S%d=%08x ord=%d +%x "), &label, r, regs[r], best_ordinal[r], regs[r] - best[r]);
        Append(line, best_file[r]);
        Line(line);
    }
}

void ReportContext(RThread& thread) {
    TBuf8<128> context;
    thread.Context(context);
    const TUint32* regs = reinterpret_cast<const TUint32*>(context.Ptr());
    const TInt words = context.Length() / 4;
    for (TInt i = 0; i < words; i += 6) {
        TBuf8<160> line(_L8("regs"));
        for (TInt j = i; j < i + 6 && j < words; ++j) line.AppendFormat(_L8(" r%d=%08x"), j, regs[j]);
        Line(line);
    }
    if (words < 16) return;
    const TUint pc = regs[15] & ~3u;
    if (pc >= 0x80000000u && pc < 0xa0000000u)
        for (TUint at = pc - 0x20; at < pc + 0x20; at += 16) {
            const TUint32* code = reinterpret_cast<const TUint32*>(at);
            LOG(_L8("rom %08x: %08x %08x %08x %08x"), at, code[0], code[1], code[2], code[3]);
        }
    MapAddresses(regs, words, _L8("r"));
}

struct Watched {
    RProcess process;
    TUint id = 0;
    RThread threads[24];
    TUint thread_ids[24] = {};
    bool reported[24] = {};
    // The last distinct pc/lr pairs of each thread, written out only if it panics.
    TUint32 trail[24][kTrail][2] = {};
    TInt trail_next[24] = {};
    TInt thread_count = 0;
};
Watched gWatched[40];

void Discover(Watched& item) {
    TFullName pattern(item.process.FullName());
    pattern.Append(_L("::*"));
    TFindThread threads(pattern);
    TFullName name;
    while (threads.Next(name) == KErrNone && item.thread_count < 24) {
        RThread thread;
        if (thread.Open(threads) != KErrNone) continue;
        RProcess owner;
        const bool ours = thread.Process(owner) == KErrNone && static_cast<TUint>(owner.Id().Id()) == item.id;
        owner.Close();
        if (!ours) {
            thread.Close();
            continue;
        }
        const TUint tid = static_cast<TUint>(thread.Id().Id());
        bool seen = false;
        for (TInt i = 0; i < item.thread_count; ++i) seen = seen || item.thread_ids[i] == tid;
        if (seen) {
            thread.Close();
            continue;
        }
        TBuf8<64> prefix;
        prefix.AppendFormat(_L8("thread pid=%u tid=%u"), item.id, tid);
        LogWithName(prefix, name);
        item.thread_ids[item.thread_count] = tid;
        item.reported[item.thread_count] = false;
        item.trail_next[item.thread_count] = 0;
        Mem::FillZ(item.trail[item.thread_count], sizeof(item.trail[0]));
        item.threads[item.thread_count++] = thread;
    }
}

void Sample(Watched& item) {
    for (TInt i = 0; i < item.thread_count; ++i) {
        RThread& thread = item.threads[i];
        if (thread.ExitType() != EExitPending) continue;
        TBuf8<128> context;
        thread.Context(context);
        if (context.Length() < 64) continue;
        const TUint32* regs = reinterpret_cast<const TUint32*>(context.Ptr());
        TUint32* previous = item.trail[i][(item.trail_next[i] + kTrail - 1) % kTrail];
        if (previous[0] == regs[15] && previous[1] == regs[14]) continue;
        TUint32* slot = item.trail[i][item.trail_next[i]];
        slot[0] = regs[15];
        slot[1] = regs[14];
        item.trail_next[i] = (item.trail_next[i] + 1) % kTrail;
    }
}

void ReportTrail(Watched& item, TInt i) {
    TUint32 addresses[kTrail * 2];
    TInt count = 0;
    for (TInt n = 0; n < kTrail; ++n) {
        const TUint32* entry = item.trail[i][(item.trail_next[i] + n) % kTrail];
        if (!entry[0]) continue;
        LOG(_L8("trail%d pc=%08x lr=%08x"), count / 2, entry[0], entry[1]);
        addresses[count++] = entry[0];
        addresses[count++] = entry[1];
    }
    // Even indices are pcs, odd ones lrs: trail entry n is t(2n) and t(2n+1).
    MapAddresses(addresses, count, _L8("t"));
}

bool Check(Watched& item) {
    for (TInt i = 0; i < item.thread_count; ++i) {
        RThread& thread = item.threads[i];
        if (item.reported[i] || thread.ExitType() == EExitPending) continue;
        item.reported[i] = true;
        const TExitCategoryName category = thread.ExitCategory();
        TBuf8<96> prefix;
        prefix.AppendFormat(_L8("thread.exit tid=%u type=%d reason=%d"), item.thread_ids[i], thread.ExitType(), thread.ExitReason());
        LogWithName(prefix, category);
        if (thread.ExitType() == EExitPanic || thread.ExitType() == EExitTerminate) {
            ReportTrail(item, i);
            ReportContext(thread);
            LogRam("at-exit");
        }
    }
    if (item.process.ExitType() == EExitPending) return false;
    const TExitCategoryName category = item.process.ExitCategory();
    TBuf8<96> prefix;
    prefix.AppendFormat(_L8("process.exit pid=%u type=%d reason=%d"), item.id, item.process.ExitType(), item.process.ExitReason());
    LogWithName(prefix, category);
    for (TInt i = 0; i < item.thread_count; ++i) item.threads[i].Close();
    item.process.Close();
    item.id = 0;
    item.thread_count = 0;
    return true;
}

bool Adopt(RProcess& process, TUint id) {
    Watched* slot = nullptr;
    for (Watched& item : gWatched) if (!item.id) { slot = &item; break; }
    const TFileName image = process.FileName();
    TBuf8<128> prefix;
    TModuleMemoryInfo info;
    const TInt memory = process.GetMemoryInfo(info);
    prefix.AppendFormat(_L8("process pid=%u sid=%08x code=%08x size=%x data=%08x memerr=%d"), id,
                        process.SecureId().iId, info.iCodeBase, info.iCodeSize, info.iInitialisedDataBase, memory);
    if (!slot) prefix.Append(_L8(" unwatched"));
    LogWithName(prefix, image);
    LogRam("at-start");
    if (!slot) {
        process.Close();
        return false;
    }
    slot->process = process;
    slot->id = id;
    slot->thread_count = 0;
    Discover(*slot);
    return true;
}

int Watch() {
    LOG(_L8("start build=%d"), kBuild);
    LogRam("baseline");
#ifdef CRASH_WATCH_TEST
    {
        RLibrary euser;
        if (euser.Load(_L("euser.dll")) == KErrNone) {
            const TUint32 probe[2] = {(reinterpret_cast<TUint32>(euser.Lookup(100)) & ~1u) + 6, 0x1234};
            LOG(_L8("test euser ord100=%08x"), reinterpret_cast<TUint32>(euser.Lookup(100)));
            MapAddresses(probe, 2, _L8("t"));
            euser.Close();
        }
    }
#endif
    const TUint self = static_cast<TUint>(RProcess().Id().Id());
    static TUint baseline[1024];
    TInt baseline_count = 0;
    bool first = true;
    TInt idle = 0;
    TInt poll = 0;
    while (idle < kIdleLimitPolls) {
        TFindProcess find(_L("*"));
        TFullName name;
        while (find.Next(name) == KErrNone) {
            RProcess process;
            const TInt opened = process.Open(find);
            if (opened != KErrNone) {
                if (first) LOG(_L8("open err=%d"), opened);
                continue;
            }
            const TUint id = static_cast<TUint>(process.Id().Id());
            bool known = id == self || process.ExitType() != EExitPending;
            for (Watched& item : gWatched) known = known || item.id == id;
            for (TInt i = 0; i < baseline_count; ++i) known = known || baseline[i] == id;
            if (known) {
                process.Close();
                continue;
            }
            const TFileName image = process.FileName();
            const bool rom = image.Length() > 0 && (image[0] == 'Z' || image[0] == 'z');
            if (first || rom) {
                TBuf8<48> prefix;
                prefix.AppendFormat(first ? _L8("baseline pid=%u") : _L8("rom pid=%u"), id);
                LogWithName(prefix, image.Length() ? image : name);
                if (baseline_count < 1024) baseline[baseline_count++] = id;
                process.Close();
                continue;
            }
            if (!Adopt(process, id) && baseline_count < 1024) baseline[baseline_count++] = id;
        }
        first = false;
        bool active = false;
        for (Watched& item : gWatched) {
            if (!item.id) continue;
            active = true;
            if (poll % 4 == 0) Discover(item);
            Sample(item);
            if (Check(item)) LogRam("after-exit");
        }
        if (active && poll % 10 == 0) RecordLibraries();
        if (active && poll % 100 == 0) LogRam("running");
        idle = active ? 0 : idle + 1;
        ++poll;
        User::After(kPollMicroseconds);
    }
    LOG(_L8("stop idle"));
    return KErrNone;
}
}

// Opening the app only starts a detached watcher process, so closing or replacing
// the foreground app does not end the watch.
int main() {
    if (User::CommandLineLength() == 0) {
        RProcess watcher;
        const TInt created = watcher.Create(RProcess().FileName(), _L("watch"));
        if (created != KErrNone) return created;
        watcher.Resume();
        watcher.Close();
#ifdef CRASH_WATCH_TEST
        User::After(2000000);
        RProcess games;
        if (games.Create(_L("playserver.exe"), KNullDesC) == KErrNone) { games.Resume(); games.Close(); }
        User::After(5000000);
        RProcess crasher;
        if (crasher.Create(RProcess().FileName(), _L("crash")) == KErrNone) { crasher.Resume(); crasher.Close(); }
        User::After(600000000);
#endif
        return KErrNone;
    }
#ifdef CRASH_WATCH_TEST
    TBuf<8> command;
    User::CommandLine(command);
    if (command == _L("crash")) {
        User::After(3000000);
        *reinterpret_cast<volatile TInt*>(4) = 1;
    }
#endif
    RMutex single;
    if (single.CreateGlobal(KMutex) != KErrNone) return KErrNone;
    if (gFs.Connect() != KErrNone) return KErrGeneral;
    gFs.MkDirAll(KLogFile);
    const TInt result = Watch();
    gFs.Close();
    single.Close();
    return result;
}
