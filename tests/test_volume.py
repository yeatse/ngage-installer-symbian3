"""Run the shared guest volume conversion against a wide host reference."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VolumeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'A host C++ compiler is needed for the shared arithmetic check')
    def test_legacy_units_preserve_mute_and_scale_to_device_maximum(self):
        source = r'''
#include "native/audio/volume.h"
#include <cassert>
#include <climits>
int main() {
    const int maxima[] = {-1, 0, 1, 3, 7, 10, 100, 10000, INT_MAX};
    for (const int maximum : maxima) {
        for (int request = -2; request <= 12; ++request) {
            const int clamped = request < 0 ? 0 : request > 10 ? 10 : request;
            const int expected = maximum <= 0 ? 0 : (static_cast<long long>(maximum) * clamped + 5) / 10;
            assert(Ngage::ScaleLegacyVolume(request, maximum) == expected);
        }
    }
    assert(Ngage::ScaleLegacyVolume(7, 10000) == 7000);
    for (int request = 0; request <= 10; ++request)
        assert(Ngage::ScaleLegacyVolume(request, 10) == request);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            output = str(Path(directory) / 'volume-test')
            subprocess.run(['c++', '-std=c++17', '-O2', '-I', str(ROOT), '-x', 'c++', '-', '-o', output],
                           input=source, text=True, check=True, capture_output=True)
            subprocess.run([output], check=True, capture_output=True)
