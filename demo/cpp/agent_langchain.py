import os
import sys
import time
import shutil

from native_layer import NativeManager
from native_layer.adapters.langchain import NativeHotReloadMiddleware


def _lib_ext() -> str:
    if sys.platform == "win32":
        return ".dll"
    if sys.platform == "darwin":
        return ".dylib"
    return ".so"


def main() -> None:
    manager = NativeManager()

    plugins_dir = os.path.abspath("./plugins")
    os.makedirs(plugins_dir, exist_ok=True)

    manager.watch_directory(plugins_dir)
    print(f"Native Manager initialized. Monitoring {plugins_dir}")

    src_plugin = os.path.abspath(f"./plugin_simsimd{_lib_ext()}")
    dst_plugin = os.path.join(plugins_dir, os.path.basename(src_plugin))

    if not os.path.exists(src_plugin):
        raise FileNotFoundError(
            f"Plugin not found: {src_plugin}. Build it first (see Commands.txt)."
        )

    shutil.copyfile(src_plugin, dst_plugin)

    deadline = time.time() + 10.0
    while time.time() < deadline:
        active = manager.get_active_tools()
        if active:
            break
        time.sleep(0.1)

    active_tools = manager.get_active_tools()
    if not active_tools:
        raise RuntimeError(
            "No C++ plugins detected. Verify plugin is present in ./plugins."
        )

    print(f"Plugins synchronized: {active_tools}")

    middleware = NativeHotReloadMiddleware(manager)
    tools = {t.name: t for t in middleware._get_live_tools()}

    tool_name = "plugin_simsimd_cosine_similarity"
    if tool_name not in tools:
        raise RuntimeError(
            f"Expected tool not found: {tool_name}. Available: {sorted(tools.keys())}"
        )

    vec_a = [1.0, 2.0, 3.0]
    vec_b = [1.0, 0.0, 3.0]

    result = tools[tool_name].invoke({"vec_a": vec_a, "vec_b": vec_b})
    print(f"Similarity: {result}")


if __name__ == "__main__":
    main()
