"""Compile and execute the white-box TTC C harnesses with a host compiler."""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parents[1]


def compiler() -> str:
    configured = os.environ.get("CC")
    if configured:
        return configured
    for candidate in ("cc", "gcc", "clang"):
        found = shutil.which(candidate)
        if found:
            return found
    raise SystemExit("No host C compiler found (set CC, or install gcc/clang)")


def compile_and_run(source: Path, output: Path) -> None:
    command = [
        compiler(),
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-I",
        str(HERE / "include"),
        "-I",
        str(FIRMWARE / "Core" / "Inc"),
        str(source),
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(output)], check=True)


def main() -> None:
    suffix = ".exe" if os.name == "nt" else ""
    with tempfile.TemporaryDirectory(prefix="ttc-host-tests-") as temporary:
        output = Path(temporary)
        compile_and_run(
            HERE / "test_lora_driver_state_machine.c",
            output / ("test_lora_driver" + suffix),
        )
        compile_and_run(
            HERE / "test_ttc_state_machine.c",
            output / ("test_ttc" + suffix),
        )
    print("TTC host C harnesses passed")


if __name__ == "__main__":
    main()
