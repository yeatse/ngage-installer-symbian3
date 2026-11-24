#pragma once
#include <e32std.h>

namespace Ngage {
// Property of the game's SID holding the process ID of a game whose video player owns its window.
// The 808's CVideoPlayerUtility leaves with KErrNotSupported on a window with a background surface.
constexpr TUint kVideoWindowKey = 0xe000096a;
}
