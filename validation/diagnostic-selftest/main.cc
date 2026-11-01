#include <e32std.h>
int main() {
    RProcess helper;
    TInt error = helper.Create(_L("C:\\sys\\bin\\ngage_touch.exe"), _L("--self-test"));
    if (error != KErrNone) return error;
    helper.Resume(); helper.Close();
    User::After(30000000);
    return 0;
}
