#include <e32std.h>
#include <e32debug.h>
#include <apgcli.h>
int main() {
    RApaLsSession apps;
    TInt error = apps.Connect();
    if (error == KErrNone) {
        TThreadId thread;
        error = apps.StartDocument(TPtrC(reinterpret_cast<const TUint16*>(u"E:\\Data\\ngage-device-test.sis")), TUid::Uid(0x101f875a), thread);
        apps.Close();
    }
    RDebug::Print(TPtrC(reinterpret_cast<const TUint16*>(u"Native SIS UI launch result %d")), error);
    User::After(180000000);
    return error;
}
