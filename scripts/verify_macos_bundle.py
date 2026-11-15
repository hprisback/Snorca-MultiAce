#!/usr/bin/env python3
"""Reject non-portable or wrong-architecture Mach-O payloads before packaging."""
import pathlib
import re
import subprocess
import sys


def output(*args):
    return subprocess.check_output(args, text=True)


def main():
    root = pathlib.Path(sys.argv[1]).resolve(strict=True)
    arch = sys.argv[2]
    if arch not in {"arm64", "x86_64"}:
        raise SystemExit("Expected arm64 or x86_64")
    errors = []
    count = 0
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            try:
                path.resolve(strict=True).relative_to(root)
            except (ValueError, FileNotFoundError, RuntimeError):
                errors.append(f"Broken or external symlink: {path.relative_to(root)}")
            continue
        if not path.is_file() or "Mach-O" not in output("file", "-b", str(path)):
            continue
        count += 1
        arches = output("lipo", "-archs", str(path)).split()
        print(f"{' '.join(arches)}\t{path.relative_to(root)}")
        if arch not in arches:
            errors.append(f"Missing {arch}: {path.relative_to(root)}")
        for line in output("otool", "-L", str(path)).splitlines()[1:]:
            dependency = re.split(r"\s+\(compatibility version", line.strip())[0]
            if dependency.startswith("/") and not dependency.startswith(("/usr/lib/", "/System/Library/")):
                errors.append(f"External library: {path.relative_to(root)} -> {dependency}")
    if not count:
        errors.append("No Mach-O executables found")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"PASS: {count} Mach-O files include {arch}; no external absolute libraries or symlinks.")


if __name__ == "__main__":
    main()
