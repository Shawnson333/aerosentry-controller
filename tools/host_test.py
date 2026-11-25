"""Build and run the host simulation of the controller logic.

Compiles the real src/statemachine.cpp, sensors.cpp, relay.cpp, radio_link.cpp
and logging.cpp with the stubs in test/host/stubs, links the plant model in
test/host/test_controller.cpp, and runs it. No board is needed.

The compiler is PlatformIO's MinGW package. Install it once with:
    pio pkg install -g --tool "platformio/toolchain-gccmingw32"

Usage:
    python tools/host_test.py          # build and run, print failures only
    python tools/host_test.py -v       # also print the controller's serial log
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    "src/statemachine.cpp",
    "src/sensors.cpp",
    "src/relay.cpp",
    "src/radio_link.cpp",
    "src/logging.cpp",
    "src/probe_care.cpp",
    "test/host/fakes.cpp",
    "test/host/test_controller.cpp",
]


def find_compiler():
    home = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    candidate = home / "packages" / "toolchain-gccmingw32" / "bin" / "g++.exe"
    if candidate.is_file():
        return candidate
    for name in ("g++", "clang++"):
        for folder in os.environ.get("PATH", "").split(os.pathsep):
            path = Path(folder) / (name + (".exe" if os.name == "nt" else ""))
            if path.is_file():
                return path
    return None


def main():
    compiler = find_compiler()
    if compiler is None:
        print("No host C++ compiler found. Run:")
        print('  pio pkg install -g --tool "platformio/toolchain-gccmingw32"')
        return 2
    out_dir = Path(tempfile.gettempdir()) / "aerosentry-host-test"
    out_dir.mkdir(exist_ok=True)
    exe = out_dir / ("host_test.exe" if os.name == "nt" else "host_test")
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    command = [
        str(compiler), "-std=gnu++14", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-I", str(ROOT / "test/host/stubs"),
        "-I", str(ROOT / "include"),
        "-I", str(ROOT / "common"),
        *[str(ROOT / source) for source in SOURCES],
        "-o", str(exe),
    ]
    print("Building host test with", compiler.name)
    build = subprocess.run(command, env=env)
    if build.returncode != 0:
        return build.returncode
    run = subprocess.run([str(exe), *sys.argv[1:]], env=env)
    return run.returncode


if __name__ == "__main__":
    sys.exit(main())
