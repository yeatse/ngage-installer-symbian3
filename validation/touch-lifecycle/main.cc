#include <e32std.h>
#include <f32file.h>
#include "../../native/common/touch_service.h"

namespace {
RFile output;
TInt failures = 0;
void Check(const TDesC8& label, bool passed) {
    output.Write(label);
    output.Write(passed ? _L8(" PASS\r\n") : _L8(" FAIL\r\n"));
    output.Flush();
    if (!passed) ++failures;
}
TInt Spawn(RProcess& process, const TDesC& path, const TDesC& arguments) {
    const TInt error = process.Create(path, arguments);
    if (error == KErrNone) process.Resume();
    return error;
}
bool WaitExit(RProcess& process, TInt expected) {
    RTimer timer;
    if (timer.CreateLocal() != KErrNone) return false;
    TRequestStatus exited, timeout;
    process.Logon(exited);
    timer.After(timeout, 5000000);
    User::WaitForRequest(exited, timeout);
    const bool completed = exited != KRequestPending;
    if (!completed) { process.LogonCancel(exited); User::WaitForRequest(exited); }
    timer.Cancel();
    if (timeout == KRequestPending) User::WaitForRequest(timeout);
    timer.Close();
    return completed && process.ExitType() == EExitKill && process.ExitReason() == expected;
}
bool Ready(RProcess& helper, TProcessId owner) {
    for (TInt i = 0; i < 30; ++i) {
        if (helper.ExitType() != EExitPending) return false;
        RMutex mutex;
        if (mutex.OpenGlobal(Ngage::TouchServiceMutex(owner)) == KErrNone) {
            mutex.Close();
            User::After(500000);
            return helper.ExitType() == EExitPending;
        }
        User::After(100000);
    }
    return false;
}
TInt Helper(RProcess& helper, RProcess& owner) {
    TBuf<32> command;
    command.AppendNum(owner.Id().Id());
    return Spawn(helper, _L("C:\\sys\\bin\\ngage_touch.exe"), command);
}
void Close(RProcess& process) {
    if (process.Handle()) {
        if (process.ExitType() == EExitPending) process.Kill(KErrCancel);
        process.Close();
    }
}
}
int main() {
    TBuf<32> command;
    User::CommandLine(command);
    if (command == _L("--owner")) { User::After(60000000); return 0; }
    if (command == _L("--short-owner")) { User::After(3000000); return 0; }
    RFs fs;
    if (fs.Connect() != KErrNone) return 1;
    fs.MkDirAll(_L("E:\\Data\\"));
    if (output.Replace(fs, _L("E:\\Data\\touch-lifecycle.txt"), EFileWrite | EFileShareAny) != KErrNone) return 2;
    RProcess invalid, owner1, owner2, helper1, helper2, duplicate, short_owner, short_helper;
    Check(_L8("no-argument-create"), Spawn(invalid, _L("C:\\sys\\bin\\ngage_touch.exe"), KNullDesC) == KErrNone);
    Check(_L8("no-argument-exits"), WaitExit(invalid, KErrArgument));
    invalid.Close();
    const TFileName self = RProcess().FileName();
    Check(_L8("owner1-create"), Spawn(owner1, self, _L("--owner")) == KErrNone);
    Check(_L8("helper1-create"), Helper(helper1, owner1) == KErrNone);
    Check(_L8("helper1-alive-without-focus"), Ready(helper1, owner1.Id()));
    Check(_L8("duplicate-create"), Helper(duplicate, owner1) == KErrNone);
    Check(_L8("duplicate-exits"), WaitExit(duplicate, KErrNone));
    Check(_L8("owner2-create"), Spawn(owner2, self, _L("--owner")) == KErrNone);
    Check(_L8("helper2-create"), Helper(helper2, owner2) == KErrNone);
    Check(_L8("helper2-alive-without-focus"), Ready(helper2, owner2.Id()));
    owner1.Kill(KErrCancel);
    Check(_L8("helper1-exits-on-owner-kill"), WaitExit(helper1, KErrNone));
    Check(_L8("helper2-survives-other-owner-exit"), helper2.ExitType() == EExitPending);
    owner2.Kill(KErrCancel);
    Check(_L8("helper2-exits-on-owner-kill"), WaitExit(helper2, KErrNone));
    Check(_L8("short-owner-create"), Spawn(short_owner, self, _L("--short-owner")) == KErrNone);
    Check(_L8("short-helper-create"), Helper(short_helper, short_owner) == KErrNone);
    Check(_L8("short-helper-ready"), Ready(short_helper, short_owner.Id()));
    Check(_L8("short-helper-exits-on-normal-return"), WaitExit(short_helper, KErrNone));
    Close(invalid); Close(owner1); Close(owner2); Close(helper1); Close(helper2);
    Close(duplicate); Close(short_owner); Close(short_helper);
    output.Close(); fs.Close();
    return failures;
}
