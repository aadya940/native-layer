# conftest.py

import sys
import pytest

sys.path.insert(0, "./native_layer/build/Release")

PLUGIN_PATH = "./native_layer/tests/plugins/libmath.dll"
PLUGIN_NAME = "math"


@pytest.fixture(scope="session")
def manager():
    """Single NativeManager for the whole test session."""
    import native_plugin_layer
    mgr = native_plugin_layer.NativeManager()
    mgr.load_plugin(PLUGIN_NAME, PLUGIN_PATH)
    yield mgr
