import sys
import pytest

PLUGIN_PATH = "./native_layer/tests/plugins/libmath.dll"
PLUGIN_NAME = "math"


@pytest.fixture(scope="session")
def manager():
    from native_layer import NativeManager
    mgr = NativeManager()
    mgr.load_plugin(PLUGIN_NAME, PLUGIN_PATH)
    yield mgr
