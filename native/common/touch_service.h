#pragma once
#include <e32std.h>

namespace Ngage {
inline TBuf<64> TouchServiceMutex(TProcessId game) {
    TBuf<64> name(_L("NgageTouchControls-"));
    name.AppendNum(game.Id());
    return name;
}
}
