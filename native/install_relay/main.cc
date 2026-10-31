#include <e32std.h>

int main() {
    RProcess server;
    const TPtrC executable(reinterpret_cast<const TUint16*>(u"installserver.exe"));
    TInt result = server.Create(executable, KNullDesC);
    if (result == KErrNone) {
        TRequestStatus status;
        server.Rendezvous(status);
        if (status != KRequestPending) {
            server.Kill(KErrGeneral);
        } else {
            server.Resume();
        }
        User::WaitForRequest(status);
        result = server.ExitType() == EExitPanic ? KErrGeneral : status.Int();
        server.Close();
    }
    RProcess::Rendezvous(result);
    return result;
}
