#pragma once
#include <e32std.h>
#include <f32file.h>

namespace Ngage {
// No shared file handle or buffered user-space output survives a panic.
inline void Trace(const char* area, const char* event, TInt a = 0, TInt b = 0, TInt c = 0,
                  const TDesC* detail = nullptr) {
#if NGAGE_DIAGNOSTIC
    RFs fs;
    if (fs.Connect() != KErrNone) return;
    RFile file;
    TInt result = KErrNotFound;
    for (TInt drive = 0; drive < 2; ++drive) {
        TFileName directory(drive == 0 ? _L("E:\\logs\\ngage\\") : _L("C:\\logs\\ngage\\"));
        fs.MkDirAll(directory);
        for (const char* p = area; *p; ++p) directory.Append(TChar(*p));
        directory.Append(_L(".log"));
        result = file.Open(fs, directory, EFileWrite | EFileShareAny);
        if (result == KErrNotFound) result = file.Create(fs, directory, EFileWrite | EFileShareAny);
        if (result == KErrNone) break;
    }
    if (result == KErrNone) {
        TBuf8<384> line;
        line.AppendNum(User::TickCount());
        line.Append(_L8(" pid=")); line.AppendNum(static_cast<TUint>(RProcess().Id().Id()), EHex);
        line.Append(_L8(" tid=")); line.AppendNum(static_cast<TUint>(RThread().Id().Id()), EHex);
        line.Append(' '); line.Append(TPtrC8(reinterpret_cast<const TUint8*>(event)));
        line.Append(_L8(" a=")); line.AppendNum(a);
        line.Append(_L8(" b=")); line.AppendNum(b);
        line.Append(_L8(" c=")); line.AppendNum(c);
        if (detail) {
            line.Append(' ');
            for (TInt i = 0; i < detail->Length() && line.Length() < 360; ++i)
                line.Append(static_cast<TUint8>((*detail)[i] < 128 ? (*detail)[i] : '?'));
        }
        line.Append(13); line.Append(10);
        TInt offset = 0;
        if (file.Seek(ESeekEnd, offset) == KErrNone) {
            file.Write(line); file.Flush();
        }
        file.Close();
    }
    fs.Close();
#else
    (void)area; (void)event; (void)a; (void)b; (void)c; (void)detail;
#endif
}
}
