#!/usr/bin/env python3
"""Enforce dynamic-memory policy for embedded-sensitive code paths.

The check scans Zephyr-related source files and public modem headers for
forbidden dynamic-allocation constructs.

Allow a specific line only when justified by appending:
    // dynamic-memory-allow: <reason>
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

TARGET_GLOBS = (
    "src/hal/zephyr_*.cpp",
    "src/zephyr_*.cpp",
    "include/modem/*.h",
    "include/hal/*.h",
)

ALLOW_MARKER = "dynamic-memory-allow"

RULES: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("std::make_unique", re.compile(r"\bstd::make_unique\s*<")),
    ("std::make_shared", re.compile(r"\bstd::make_shared\s*<")),
    ("std::allocate_shared", re.compile(r"\bstd::allocate_shared\s*<")),
    ("std::vector", re.compile(r"\bstd::vector\s*<")),
    ("std::string", re.compile(r"\bstd::string\b")),
    ("std::deque", re.compile(r"\bstd::deque\s*<")),
    ("std::list", re.compile(r"\bstd::list\s*<")),
    ("std::map", re.compile(r"\bstd::map\s*<")),
    ("std::unordered_map", re.compile(r"\bstd::unordered_map\s*<")),
    ("std::set", re.compile(r"\bstd::set\s*<")),
    ("std::unordered_set", re.compile(r"\bstd::unordered_set\s*<")),
    # Match operator new usage.
    ("new", re.compile(r"\bnew\b\s*(\[)?")),
)


def iter_target_files() -> list[Path]:
    files: set[Path] = set()
    for pattern in TARGET_GLOBS:
        files.update(ROOT.glob(pattern))
    return sorted(files)


def code_portion(line: str) -> str:
    return line.split("//", 1)[0]


def main() -> int:
    violations: list[str] = []

    for file_path in iter_target_files():
        text = file_path.read_text(encoding="utf-8", errors="ignore")
        for line_no, line in enumerate(text.splitlines(), start=1):
            if ALLOW_MARKER in line:
                continue

            code = code_portion(line)
            if not code.strip():
                continue

            for rule_name, pattern in RULES:
                if pattern.search(code):
                    rel = file_path.relative_to(ROOT).as_posix()
                    violations.append(
                        f"{rel}:{line_no}: forbidden dynamic-memory construct '{rule_name}'"
                    )
                    break

    if violations:
        print("Dynamic memory policy violations found:")
        for entry in violations:
            print(f"  - {entry}")
        print(
            "\nIf a specific line is intentionally exempt (e.g., factory boundary), "
            f"add '{ALLOW_MARKER}: <reason>' on that line."
        )
        return 1

    print("Dynamic memory policy check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
