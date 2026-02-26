try:
    from .native_plugin_layer import NativeManager
except ImportError:
    class NativeManager:
        def __init__(self, *args, **kwargs):
            raise RuntimeError(
                "Native Layer not installed! Run `pip install .` to compile the C++ engine."
            )

__all__ = ["NativeManager"]
