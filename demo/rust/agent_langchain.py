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


def _find_built_plugin(built_dir: str) -> str:
    ext = _lib_ext()
    candidates = [
        os.path.join(built_dir, f"plugin_rust{ext}"),
        os.path.join(built_dir, f"libplugin_rust{ext}"),
    ]
    for p in candidates:
        if os.path.exists(p):
            return p
    raise FileNotFoundError(
        f"Plugin not found in {built_dir}. Tried: {candidates}. Build it first (see Commands.txt)."
    )


def main() -> None:
    manager = NativeManager()

    plugins_dir = os.path.abspath("./plugins")
    os.makedirs(plugins_dir, exist_ok=True)

    manager.watch_directory(plugins_dir)
    print(f"Native Manager initialized. Monitoring {plugins_dir}")

    built_dir = os.path.abspath("./target/release")
    src_plugin = _find_built_plugin(built_dir)
    dst_plugin = os.path.join(plugins_dir, os.path.basename(src_plugin))

    shutil.copyfile(src_plugin, dst_plugin)

    active_tools = manager.get_active_tools()

    # Give the manager a moment to process the copy.
    time.sleep(0.2)

    print(f"Plugins synchronized: {active_tools}")

    middleware = NativeHotReloadMiddleware(manager)
    tools = {t.name: t for t in middleware._get_live_tools()}

    pretty_name = "plugin_rust_json_pretty"
    get_name = "plugin_rust_json_get"

    raw = '{"a": {"b": {"c": 123}}, "msg": "hi"}'
    pretty = tools[pretty_name].invoke({"json": raw})
    print("Pretty JSON:")
    print(pretty)

    extracted = tools[get_name].invoke({"json": raw, "path": "a.b.c"})
    print(f"Extracted (a.b.c): {extracted}")


if __name__ == "__main__":
    main()
