#!/usr/bin/env python3
"""Build, launch, and benchmark the native Metal Gargantua renderer."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
from typing import Iterable, Sequence


PROJECT_ROOT = Path(__file__).resolve().parent
RELEASE_BINARY = PROJECT_ROOT / ".build" / "release" / "BlackHoleApp"
RESOURCE_BUNDLE = PROJECT_ROOT / ".build" / "release" / "BlackHole_BlackHoleApp.bundle"
DIST_APP = PROJECT_ROOT / "dist" / "Gargantua.app"


def parse_resolution(value: str) -> tuple[int, int]:
    try:
        width_text, height_text = value.lower().split("x", 1)
        width, height = int(width_text), int(height_text)
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError("resolution must look like 2560x1440") from error
    if not 640 <= width <= 7680 or not 360 <= height <= 4320:
        raise argparse.ArgumentTypeError("resolution must be between 640x360 and 7680x4320")
    return width, height


def project_sources() -> Iterable[Path]:
    yield PROJECT_ROOT / "Package.swift"
    yield from (PROJECT_ROOT / "Sources").rglob("*")


def needs_rebuild(binary: Path = RELEASE_BINARY) -> bool:
    if not binary.exists():
        return True
    binary_time = binary.stat().st_mtime
    return any(path.is_file() and path.stat().st_mtime > binary_time for path in project_sources())


def build_release() -> Path:
    print("Building the native Metal renderer...", flush=True)
    subprocess.run(
        ["swift", "build", "-c", "release"],
        cwd=PROJECT_ROOT,
        check=True,
    )
    if not RELEASE_BINARY.exists():
        raise RuntimeError(f"Swift reported success but {RELEASE_BINARY} is missing")
    return RELEASE_BINARY


def package_app() -> Path:
    if needs_rebuild():
        build_release()
    if not RESOURCE_BUNDLE.exists():
        raise RuntimeError(f"Swift resource bundle is missing: {RESOURCE_BUNDLE}")

    if DIST_APP.exists():
        shutil.rmtree(DIST_APP)
    macos_directory = DIST_APP / "Contents" / "MacOS"
    resources_directory = DIST_APP / "Contents" / "Resources"
    macos_directory.mkdir(parents=True)
    resources_directory.mkdir(parents=True)
    executable = macos_directory / "Gargantua"
    shutil.copy2(RELEASE_BINARY, executable)
    executable.chmod(0o755)
    # SwiftPM's generated Bundle.module first resolves this bundle beside Bundle.main.
    shutil.copytree(RESOURCE_BUNDLE, DIST_APP / RESOURCE_BUNDLE.name)
    with (DIST_APP / "Contents" / "Info.plist").open("wb") as handle:
        plistlib.dump(
            {
                "CFBundleDevelopmentRegion": "en",
                "CFBundleDisplayName": "Gargantua",
                "CFBundleExecutable": "Gargantua",
                "CFBundleIdentifier": "com.alturatech.gargantua",
                "CFBundleInfoDictionaryVersion": "6.0",
                "CFBundleName": "Gargantua",
                "CFBundlePackageType": "APPL",
                "CFBundleShortVersionString": "1.0",
                "CFBundleVersion": "1",
                "LSMinimumSystemVersion": "14.0",
                "NSHighResolutionCapable": True,
                "NSHumanReadableCopyright": "Kerr spacetime visualization",
                "NSSupportsAutomaticGraphicsSwitching": False,
            },
            handle,
            sort_keys=True,
        )
    return DIST_APP


def native_arguments(args: argparse.Namespace) -> list[str]:
    width, height = args.resolution
    native = [
        "--width", str(width),
        "--height", str(height),
        "--target-fps", str(args.target_fps),
        "--render-scale", str(args.render_scale),
        "--steps", str(args.steps),
        "--spin", str(args.spin),
        "--exposure", str(args.exposure),
    ]
    if args.fullscreen:
        native.append("--fullscreen")
    if args.no_overlay:
        native.append("--no-overlay")
    if args.no_adaptive:
        native.append("--no-adaptive")
    if args.command == "benchmark":
        native.extend(["--benchmark-seconds", str(args.seconds)])
    return native


def run_renderer(args: argparse.Namespace) -> int:
    binary = RELEASE_BINARY
    if not args.no_build and needs_rebuild(binary):
        binary = build_release()
    if not binary.exists():
        print("Renderer is not built. Run `python3 blackhole.py build`.", file=sys.stderr)
        return 2
    command = [str(binary), *native_arguments(args)]
    return subprocess.run(command, cwd=PROJECT_ROOT, check=False).returncode


def doctor() -> int:
    checks = {
        "macOS": platform.system() == "Darwin",
        "Apple Silicon": platform.machine() == "arm64",
        "Swift": shutil.which("swift") is not None,
        "Xcode toolchain": shutil.which("xcrun") is not None,
    }
    metal_path = "unavailable"
    if checks["Xcode toolchain"]:
        result = subprocess.run(
            ["xcrun", "--find", "metal"],
            capture_output=True,
            text=True,
            check=False,
        )
        metal_path = result.stdout.strip() if result.returncode == 0 else "unavailable"
        checks["Metal compiler"] = result.returncode == 0

    print("Gargantua system check")
    for name, passed in checks.items():
        print(f"  {'OK' if passed else 'FAIL':4}  {name}")
    print(f"  Metal tool: {metal_path}")

    if platform.system() == "Darwin" and shutil.which("system_profiler"):
        profile = subprocess.run(
            ["system_profiler", "SPHardwareDataType", "SPDisplaysDataType"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout
        for line in profile.splitlines():
            stripped = line.strip()
            if stripped.startswith(("Chip:", "Total Number of Cores:", "Metal Support:", "Resolution:", "Display Type:")):
                print(f"  {stripped}")
    return 0 if all(checks.values()) else 1


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Real-time Kerr black hole ray tracer for Apple Metal and XDR displays."
    )
    parser.add_argument("command", nargs="?", choices=("run", "benchmark", "build", "package", "doctor"), default="run")
    parser.add_argument("--resolution", type=parse_resolution, default=(2560, 1440), metavar="WIDTHxHEIGHT")
    parser.add_argument("--target-fps", type=int, default=120)
    parser.add_argument("--render-scale", type=float, default=1.0)
    parser.add_argument("--steps", type=int, default=192)
    parser.add_argument("--spin", type=float, default=0.998)
    parser.add_argument("--exposure", type=float, default=1.0)
    parser.add_argument("--seconds", type=float, default=8.0, help="benchmark duration")
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--no-overlay", action="store_true")
    parser.add_argument("--no-adaptive", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    if args.command == "doctor":
        return doctor()
    if args.command == "build":
        build_release()
        return 0
    if args.command == "package":
        print(package_app())
        return 0
    return run_renderer(args)


if __name__ == "__main__":
    raise SystemExit(main())
