#pragma once
#include <e32std.h>

namespace Ngage {
// Property of the game's SID: the game's process ID while its video player needs the window, the
// negated ID once the posting surface must be shown again. The 808's CVideoPlayerUtility neither
// starts nor plays on a window with another background surface.
constexpr TUint kVideoWindowKey = 0xe000096a;
}
