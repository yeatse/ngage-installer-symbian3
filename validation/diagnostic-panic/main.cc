#include <e32std.h>
#include "../../native/posting/Posting_Surface_Factory.h"
int main() {
    RLibrary library;
    TInt result = library.Load(_L("PostingSurfaceFactory.dll"));
    if (result != KErrNone) return result;
    typedef CPostingSurfaceFactory* (*Factory)();
    auto factory = reinterpret_cast<Factory>(library.Lookup(1))();
    if (!factory) return KErrNoMemory;
    User::After(10000000);
    User::Panic(_L("KERN-EXEC"), 3);
    return 0;
}
