import os
import sys
import shutil
import subprocess
import pytest
from pathlib import Path

IS_WINDOWS = sys.platform == "win32"
IS_MACOS   = sys.platform == "darwin"
IS_LINUX   = sys.platform.startswith("linux")

LIB_EXT = {
    "win32":  ".dll",
    "darwin": ".dylib",
}.get(sys.platform, ".so")

PLUGIN_SRC  = Path("native_layer/tests/plugins/libmath.cpp")
PLUGIN_PATH = PLUGIN_SRC.with_suffix(LIB_EXT)
PLUGIN_NAME = "math"


def _compile_windows(src: Path, out: Path) -> bool:
    env = os.environ.copy()

    try:
        from setuptools import msvc as _msvc
        vc_env = _msvc.EnvironmentInfo("x64").return_env()
        for k, v in vc_env.items():
            env[k.upper()] = v
    except Exception as e:
        print(f"  Warning: could not locate MSVC env via setuptools: {e}")

    cl = shutil.which("cl.exe", path=env.get("PATH")) or "cl.exe"

    cmd = [
        cl, "/LD", "/EHsc", "/std:c++17",
        str(src),
        "/I", "native_layer/include",
        f"/Fe:{out}",
    ]
    return _run(cmd, env=env)


def _compile_unix(src: Path, out: Path) -> bool:
    compiler = shutil.which("g++") or shutil.which("c++") or shutil.which("clang++")
    if not compiler:
        print("  Error: no C++ compiler found (tried g++, c++, clang++).")
        return False

    extra = ["-undefined", "dynamic_lookup"] if IS_MACOS else []

    cmd = [
        compiler, "-shared", "-fPIC", "-std=c++17",
        str(src),
        "-I", "native_layer/include",
        "-o", str(out),
        *extra,
    ]
    return _run(cmd)


def _run(cmd: list, env: dict = None) -> bool:
    try:
        result = subprocess.run(
            cmd,
            env=env,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            return True
        print(f"  Compilation failed (exit {result.returncode})")
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)
        return False
    except FileNotFoundError:
        print(f"  Error: compiler not found: {cmd[0]}")
        return False
    except Exception as e:
        print(f"  Error during compilation: {e}")
        return False


def compile_plugin():
    if not PLUGIN_SRC.exists():
        print(f"  Warning: plugin source not found: {PLUGIN_SRC}")
        return

    if (
        PLUGIN_PATH.exists()
        and PLUGIN_PATH.stat().st_mtime > PLUGIN_SRC.stat().st_mtime
    ):
        return

    print(f"\nCompiling {PLUGIN_SRC} -> {PLUGIN_PATH}")

    if IS_WINDOWS:
        ok = _compile_windows(PLUGIN_SRC, PLUGIN_PATH)
    else:
        ok = _compile_unix(PLUGIN_SRC, PLUGIN_PATH)

    if ok:
        print(f"  OK: {PLUGIN_PATH}")
    else:
        print(f"  Failed: {PLUGIN_PATH} was not produced.")


def pytest_sessionstart(session):
    PLUGIN_PATH.parent.mkdir(parents=True, exist_ok=True)
    compile_plugin()


@pytest.fixture(scope="session")
def manager():
    from native_layer import NativeManager

    if not PLUGIN_PATH.exists():
        pytest.fail(
            f"Test plugin not found: {PLUGIN_PATH}\n"
            f"Compile it manually or check that a C++ compiler is on PATH."
        )

    mgr = NativeManager()
    mgr.load_plugin(PLUGIN_NAME, str(PLUGIN_PATH))
    yield mgr