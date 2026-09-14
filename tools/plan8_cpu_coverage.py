#!/usr/bin/env python3
"""Summarize Plan 8 CPU line coverage without modifying the source tree.

Each gcda is passed to gcov independently in a temporary working directory.
The JSON line records are then unioned by canonical source path and line number;
this matters for inline headers instantiated by several test translation units.
"""

from __future__ import annotations

import argparse
import gzip
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BASELINE = "abc3d21"
DEFAULT_BUILD = Path("/home/blakethefn/.cache/blackhole-renderer/plan8-coverage")
SCOPE = (
    "lib/src/appearance.cpp",
    "lib/src/image.cpp",
    "lib/src/shot_io.cpp",
    "lib/include/bhr/display_transform.hpp",
    "lib/include/bhr/disk_appearance.hpp",
)
EXCLUSIONS = (
    "tests: test translation units are drivers, not part of the scoped metric",
    "external/: vendored dependencies are excluded",
    "app/: GUI/application sources are excluded",
    "*.cu and CUDA-generated objects: GPU code is excluded from this CPU metric",
)


def canonical(path: str) -> str:
    """Return a repository-relative POSIX path for a gcov source path."""
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = ROOT / candidate
    try:
        return candidate.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return candidate.resolve().as_posix()


def added_lines(path: str) -> set[int]:
    """Read added line numbers from the baseline diff, or all lines if new."""
    exists = subprocess.run(
        ["git", "cat-file", "-e", f"{BASELINE}:{path}"],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0
    if not exists:
        return {line.number for line in Path(ROOT / path).read_text().splitlines(True)
                for line in [type("Line", (), {"number": 0})()]}

    diff = subprocess.run(
        ["git", "diff", BASELINE, "--unified=0", "--", path],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    lines: set[int] = set()
    current = 0
    for raw in diff.splitlines():
        match = re.match(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", raw)
        if match:
            current = int(match.group(1))
            continue
        if raw.startswith("+") and not raw.startswith("+++"):
            lines.add(current)
            current += 1
        elif not raw.startswith("-") and current:
            current += 1
    return lines


def all_source_lines(path: str) -> set[int]:
    """Use source line numbers for an untracked/new file baseline."""
    return set(range(1, len((ROOT / path).read_text().splitlines()) + 1))


def gcov_json(build: Path) -> list[dict]:
    records: list[dict] = []
    gcda_files = sorted(build.rglob("*.gcda"))
    if not gcda_files:
        raise RuntimeError(f"no .gcda files found beneath {build}")
    with tempfile.TemporaryDirectory(prefix="plan8-gcov-") as temporary:
        output_dir = Path(temporary)
        for gcda in gcda_files:
            result = subprocess.run(
                ["gcov", "--json-format", "--object-directory", str(gcda.parent), str(gcda)],
                cwd=output_dir,
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                raise RuntimeError(f"gcov failed for {gcda}: {result.stderr.strip()}")
            generated = sorted(output_dir.glob("*.gcov.json.gz"))
            if not generated:
                raise RuntimeError(f"gcov produced no JSON for {gcda}")
            for report in generated:
                with gzip.open(report, "rt", encoding="utf-8") as stream:
                    records.append(json.load(stream))
                report.unlink()
    return records


def summarize(build: Path) -> dict:
    wanted = set(SCOPE)
    executable: dict[str, dict[int, bool]] = {path: {} for path in SCOPE}
    for report in gcov_json(build):
        for source in report.get("files", []):
            path = canonical(source["file"])
            if path not in wanted:
                continue
            for line in source.get("lines", []):
                number = int(line["line_number"])
                executable[path][number] = executable[path].get(number, False) or int(line.get("count", 0)) > 0

    files = []
    for path in SCOPE:
        baseline_lines = added_lines(path)
        observed = executable[path]
        scoped = {line: hit for line, hit in observed.items() if line in baseline_lines}
        total = len(scoped)
        hit = sum(scoped.values())
        files.append({
            "path": path,
            "hit": hit,
            "total": total,
            "percent": round(100.0 * hit / total, 2) if total else 100.0,
            "missed_lines": sorted(line for line, covered in scoped.items() if not covered),
        })
    total = sum(item["total"] for item in files)
    hit = sum(item["hit"] for item in files)
    return {
        "schema": "plan8.cpu-coverage.v1",
        "baseline": BASELINE,
        "coverage_build": str(build),
        "tool": "gcov --json-format",
        "aggregation": "union execution by canonical source path and line across all gcov JSON translation units",
        "scope": list(SCOPE),
        "exclusions": list(EXCLUSIONS),
        "files": files,
        "aggregate": {"hit": hit, "total": total, "percent": round(100.0 * hit / total, 2) if total else 100.0},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = summarize(args.build.expanduser().resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
