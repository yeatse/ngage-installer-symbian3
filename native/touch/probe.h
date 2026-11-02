#pragma once
#include "../posting/Posting_Surface_Factory.h"
#include "../common/trace.h"

namespace Ngage {
inline TUint HeaderWord(const TDesC8& bytes, TInt offset) {
    return static_cast<TUint>(bytes[offset]) | (static_cast<TUint>(bytes[offset+1]) << 8)
        | (static_cast<TUint>(bytes[offset+2]) << 16) | (static_cast<TUint>(bytes[offset+3]) << 24);
}
inline TInt RunProbe(TInt mode) {
#if NGAGE_DIAGNOSTIC
    Trace("probe", "begin", 1549, mode);
    const TPtrC image_path(mode == 3 ? _L("E:\\sys\\bin\\ngibackbuffer.dll")
                                    : _L("C:\\sys\\bin\\PostingSurfaceFactory.dll"));
    RFs fs;
    TInt error = fs.Connect();
    Trace("probe", "fs.connect", error);
    if (error == KErrNone) {
        RFile image;
        error = image.Open(fs, image_path, EFileRead | EFileShareAny);
        Trace("probe", "image.open", error);
        if (error == KErrNone) {
            TBuf8<156> header;
            TInt size = 0; image.Size(size);
            error = image.Read(header);
            Trace("probe", "image.header", error, size, header.Length());
            if (header.Length() == 156) {
                Trace("probe", "image.uids", HeaderWord(header,0), HeaderWord(header,4), HeaderWord(header,8));
                Trace("probe", "image.sid.caps", HeaderWord(header,128), HeaderWord(header,136), HeaderWord(header,140));
                Trace("probe", "image.version.flags.cpu", HeaderWord(header,24), HeaderWord(header,44),
                      static_cast<TUint8>(header[122]) | (static_cast<TUint8>(header[123]) << 8));
                Trace("probe", "image.entry.code", HeaderWord(header,72), HeaderWord(header,100), HeaderWord(header,48));
                Trace("probe", "image.crc.compression", HeaderWord(header,20), HeaderWord(header,28));
            }
            if (size >= 0 && size <= 2 * 1024 * 1024) {
                TInt offset = 0;
                TUint hash = 2166136261u;
                TInt count = 0;
                error = image.Seek(ESeekStart, offset);
                TBuf8<512> block;
                while (error == KErrNone) {
                    error = image.Read(block);
                    if (error != KErrNone || block.Length() == 0) break;
                    for (TInt i = 0; i < block.Length(); ++i) hash = (hash ^ block[i]) * 16777619u;
                    count += block.Length();
                }
                Trace("probe", "image.fnv1a", hash, count, error);
            }
            image.Close();
        }
        fs.Close();
    }
    RLibrary library;
    Trace("probe", "load.enter", mode);
    error = library.Load(image_path);
    Trace("probe", "load.return", error, mode);
    if (error != KErrNone) return error;
    const TUidType type = library.Type();
    const TFileName path = library.FileName();
    Trace("probe", "load.identity", type[0].iUid, type[1].iUid, type[2].iUid, &path);
    if (mode == 3) {
        library.Close(); Trace("probe", "backbuffer.done"); return KErrNone;
    }
    const TLibraryFunction entry = library.Lookup(1);
    Trace("probe", "lookup.return", reinterpret_cast<TInt>(entry));
    if (!entry) { library.Close(); return KErrNotFound; }
    if (mode == 2) {
        typedef CPostingSurfaceFactory* (*Factory)();
        Trace("probe", "factory.call.enter");
        CPostingSurfaceFactory* factory = reinterpret_cast<Factory>(entry)();
        Trace("probe", "factory.call.return", reinterpret_cast<TInt>(factory));
        if (!factory) { library.Close(); return KErrNoMemory; }
        delete factory;
        Trace("probe", "factory.delete.done");
    }
    Trace("probe", "close.enter");
    library.Close();
    Trace("probe", "done", mode);
    return KErrNone;
#else
    (void)mode; return KErrNotSupported;
#endif
}
}
