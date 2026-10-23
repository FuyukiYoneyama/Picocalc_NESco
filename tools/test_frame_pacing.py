#!/usr/bin/env python3
"""Run the production display/mixer against an injected clock and DMA consumer.

No Pico SDK or hardware is required. The injected costs test scheduling under
load; the resulting frame rates are not RP2040 performance measurements.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run_tests(build_dir: Path) -> None:
    root = Path(__file__).resolve().parent.parent
    build_dir.mkdir(parents=True, exist_ok=True)
    for ring_size in (4096, 8192):
        for clock_lock in (False, True):
            name = f"frame-pacing-{ring_size}-clock-{int(clock_lock)}"
            binary = build_dir / name
            command = [
                os.environ.get("CXX", "g++"), "-std=c++17", "-O2",
                "-ffunction-sections", "-fdata-sections", "-DPICO_BUILD",
                "-DNESCO_AUDIO_PREFILL", "-DNESCO_AUDIO_FRAME_ADMISSION",
                "-Itools/tests/stubs", "-Iplatform", "-Iinfones",
                "tools/tests/frame_pacing_test.cpp", "-Wl,--gc-sections",
                "-o", str(binary),
            ]
            if ring_size == 8192:
                command.append("-DNESCO_AUDIO_RING_8192")
            if clock_lock:
                command += ["-DNESCO_AUDIO_CLOCK_LOCK", "-DNESCO_MAPPER19_AUDIO_COMFORT"]
            print(name, flush=True)
            subprocess.run(command, cwd=root, check=True)
            subprocess.run([str(binary)], cwd=root, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    if args.build_dir:
        run_tests(args.build_dir.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="nesco-frame-pacing-") as directory:
            run_tests(Path(directory))


if __name__ == "__main__":
    main()
