#!/usr/bin/env python3
"""One native, dependency-light entry point for firmware host checks."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

project = Path(__file__).resolve().parents[1]
test = project / "test"
main = project / "main"
if not os.environ.get("IDF_PATH"):
    raise SystemExit("Set IDF_PATH to the pinned ESP-IDF master (camera checks use its actual driver source)")
cjson = Path(os.environ.get("CJSON_SOURCE", project / "managed_components/espressif__cjson/cJSON/cJSON.c"))
if not cjson.is_file():
    raise SystemExit("Run idf.py --preview reconfigure to resolve locked components, or set CJSON_SOURCE")

def run(command):
    subprocess.run([str(x) for x in command], check=True, cwd=project)

cc, cxx = os.environ.get("CC", "cc"), os.environ.get("CXX", "c++")
with tempfile.TemporaryDirectory(prefix="yokai-host-") as tmp:
    tmp = Path(tmp)
    for name in ("calculator_engine", "clock_service", "fireworks_engine", "app_state"):
        binary = tmp / name
        run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DHOST_TEST", "-I", main,
             test / f"test_{name}.c", main / f"{name}.c", "-lm", "-o", binary])
        run([binary])
    binary = tmp / "weather"
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DHOST_TEST", "-I", main,
         "-I", cjson.parent, test / "test_weather_service.c", main / "weather_service.c",
         cjson, "-lm", "-o", binary])
    run([binary])
    binary = tmp / "voice"
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DVOICE_SERVICE_LOGIC_ONLY",
         "-I", main, test / "test_voice_service.c", main / "voice_service.c", "-lm", "-o", binary])
    run([binary])
    binary = tmp / "food"
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", test / "test_food_service.c", "-o", binary])
    run([binary])
    binary = tmp / "vision"
    run([cxx, "-x", "c++", "-std=c++17", "-DHOST_TEST", "-I", main,
         test / "test_vision_service.c", main / "vision_camera.c", main / "storage_service.c", "-o", binary])
    run([binary])

for script in [test / "run_synth_checks.py", test / "check_voice_integration.py", *sorted(test.glob("test_*.py"))]:
    print(f"Checking {script.name}", flush=True)
    run([sys.executable, script])
print("All Yokai host checks passed")
