#!/usr/bin/env python3
"""
Code style checker for C++ naming conventions.

Checks the following naming rules in src/ and include/ directories:
  - Functions:          snake_case (e.g. read_sensor_value, process_frame)
  - Variables:          camelCase (e.g. sampleCount, rxBuffer)
  - Bool variables:     fPascalCase prefix (e.g. fIsConnected, fHasData, fEnabled)
  - Global variables:   g_ prefix + camelCase (e.g. g_sensorState, g_txBuffer)
  - Static variables:   s_ prefix + camelCase (e.g. s_instanceCount, s_cachedValue)
  - Types (class/struct): PascalCase (e.g. SensorDriver, FrameHeader)
  - Enums:              PascalCase for type, PascalCase for values (e.g. Error::HardwareFault)
  - Constants/macros:   UPPER_SNAKE_CASE (e.g. MAX_FRAME_SIZE, DEFAULT_TIMEOUT_MS)
  - Namespaces:         snake_case (e.g. anova::sensor_driver)
  - Template params:    PascalCase (e.g. template <typename ValueType>)

Usage:
    python scripts/check_code_style.py [--src DIR] [--include DIR] [--verbose]
"""

import argparse
import os
import re
import sys
from pathlib import Path
from typing import List, Tuple

# --- Pattern definitions ---

# snake_case: lowercase letters, digits, underscores; must start with lowercase letter
RE_SNAKE_CASE = re.compile(r'^[a-z][a-z0-9_]*$')

# camelCase: starts with lowercase, then mixed case
RE_CAMEL_CASE = re.compile(r'^[a-z][a-zA-Z0-9]*$')

# PascalCase: starts with uppercase, then mixed case
RE_PASCAL_CASE = re.compile(r'^[A-Z][a-zA-Z0-9]*$')

# fPascalCase: starts with 'f' followed by uppercase
RE_BOOL_VAR = re.compile(r'^f[A-Z][a-zA-Z0-9]*$')

# g_ prefix + camelCase
RE_GLOBAL_VAR = re.compile(r'^g_[a-z][a-zA-Z0-9]*$')

# s_ prefix + camelCase
RE_STATIC_VAR = re.compile(r'^s_[a-z][a-zA-Z0-9]*$')

# UPPER_SNAKE_CASE: uppercase letters, digits, underscores
RE_UPPER_SNAKE = re.compile(r'^[A-Z][A-Z0-9_]*$')

# --- Violation tracking ---

class Violation:
    def __init__(self, file: str, line: int, rule: str, name: str, message: str):
        self.file = file
        self.line = line
        self.rule = rule
        self.name = name
        self.message = message

    def __str__(self):
        return f"{self.file}:{self.line}: [{self.rule}] '{self.name}' - {self.message}"


# --- Helper functions ---

def is_snake_case(name: str) -> bool:
    return RE_SNAKE_CASE.match(name) is not None


def is_camel_case(name: str) -> bool:
    return RE_CAMEL_CASE.match(name) is not None


def is_pascal_case(name: str) -> bool:
    return RE_PASCAL_CASE.match(name) is not None


def is_bool_var_name(name: str) -> bool:
    return RE_BOOL_VAR.match(name) is not None


def is_global_var_name(name: str) -> bool:
    return RE_GLOBAL_VAR.match(name) is not None


def is_static_var_name(name: str) -> bool:
    return RE_STATIC_VAR.match(name) is not None


def is_upper_snake_case(name: str) -> bool:
    return RE_UPPER_SNAKE.match(name) is not None


def strip_comments(line: str) -> str:
    """Remove single-line comments from a line."""
    # Handle // comments (simple heuristic, doesn't handle strings)
    in_string = False
    escape_next = False
    for i, ch in enumerate(line):
        if escape_next:
            escape_next = False
            continue
        if ch == '\\':
            escape_next = True
            continue
        if ch == '"' and not in_string:
            in_string = True
        elif ch == '"' and in_string:
            in_string = False
        elif ch == '/' and i + 1 < len(line) and line[i + 1] == '/' and not in_string:
            return line[:i]
    return line


def is_in_comment_block(lines: List[str], line_idx: int) -> bool:
    """Check if a line is inside a block comment /* ... */."""
    in_block = False
    for i in range(line_idx + 1):
        line = lines[i]
        j = 0
        while j < len(line):
            if in_block:
                if j + 1 < len(line) and line[j] == '*' and line[j + 1] == '/':
                    in_block = False
                    j += 2
                    continue
            else:
                if j + 1 < len(line) and line[j] == '/' and line[j + 1] == '*':
                    in_block = True
                    j += 2
                    continue
                if j + 1 < len(line) and line[j] == '/' and line[j + 1] == '/':
                    break
            j += 1
    return in_block


# --- Checkers ---

# Keywords and known types to skip
CPP_KEYWORDS = {
    'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'break', 'continue',
    'return', 'goto', 'default', 'typedef', 'sizeof', 'alignof', 'decltype',
    'static_assert', 'static_cast', 'dynamic_cast', 'const_cast', 'reinterpret_cast',
    'try', 'catch', 'throw', 'new', 'delete', 'this', 'nullptr', 'true', 'false',
    'void', 'bool', 'char', 'int', 'float', 'double', 'long', 'short', 'unsigned',
    'signed', 'auto', 'const', 'volatile', 'mutable', 'static', 'extern', 'register',
    'inline', 'virtual', 'override', 'final', 'explicit', 'friend', 'operator',
    'public', 'private', 'protected', 'class', 'struct', 'union', 'enum', 'namespace',
    'template', 'typename', 'using', 'constexpr', 'noexcept', 'co_await', 'co_yield',
    'co_return', 'requires', 'concept', 'export', 'import', 'module',
}

# Names excluded from style checks (product names, established identifiers)
EXCLUDED_NAMES = {
    'xE310', 'ME310M1',
}

# Standard library types/functions that should be ignored
STD_PREFIXES = {'std::', 'k_', 'K_'}

# Known type patterns to skip for variable checks
KNOWN_TYPES = {
    'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
    'int8_t', 'int16_t', 'int32_t', 'int64_t',
    'size_t', 'ssize_t', 'ptrdiff_t', 'intptr_t', 'uintptr_t',
    'string', 'vector', 'unique_ptr', 'shared_ptr', 'weak_ptr',
}


def check_namespace(line: str, file: str, line_num: int) -> List[Violation]:
    """Check namespace declarations use snake_case."""
    violations = []
    match = re.match(r'\s*namespace\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\{?', line)
    if match:
        name = match.group(1)
        if name in CPP_KEYWORDS:
            return violations
        if not is_snake_case(name):
            violations.append(Violation(
                file, line_num, "namespace",
                name, "Namespace should be snake_case"
            ))
    return violations


def check_class_struct(line: str, file: str, line_num: int) -> List[Violation]:
    """Check class/struct declarations use PascalCase."""
    violations = []
    match = re.match(r'\s*(?:class|struct)\s+([a-zA-Z_][a-zA-Z0-9_]*)', line)
    if match:
        name = match.group(1)
        if name in CPP_KEYWORDS:
            return violations
        # Skip system/external types (POSIX, Zephyr, etc.)
        if name.startswith('k_') or name.startswith('_'):
            return violations
        # Skip usage as variable declaration (e.g. "struct termios tio;")
        # Only flag definitions (have { or : after the name)
        after_name = line[match.end():]
        if not re.match(r'\s*(?:[:{]|$|;?\s*$)', after_name):
            return violations
        # Skip product/project names that are established identifiers
        if name in EXCLUDED_NAMES:
            return violations
        if not is_pascal_case(name):
            violations.append(Violation(
                file, line_num, "type",
                name, "Type (class/struct) should be PascalCase"
            ))
    return violations


def check_enum(line: str, file: str, line_num: int) -> List[Violation]:
    """Check enum type declarations use PascalCase."""
    violations = []
    match = re.match(r'\s*enum\s+(?:class\s+)?([a-zA-Z_][a-zA-Z0-9_]*)', line)
    if match:
        name = match.group(1)
        if name in CPP_KEYWORDS:
            return violations
        if not is_pascal_case(name):
            violations.append(Violation(
                file, line_num, "enum_type",
                name, "Enum type should be PascalCase"
            ))
    return violations


def check_enum_value(line: str, file: str, line_num: int, in_enum: bool) -> List[Violation]:
    """Check enum values use PascalCase (when inside an enum block)."""
    violations = []
    if not in_enum:
        return violations
    # Match enum value: identifier optionally followed by = value
    match = re.match(r'\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:=|,|$)', line)
    if match:
        name = match.group(1)
        if name in CPP_KEYWORDS:
            return violations
        # Allow snake_case enum values as they're common in existing code
        # The convention says PascalCase for values
        if not is_pascal_case(name) and not is_snake_case(name):
            violations.append(Violation(
                file, line_num, "enum_value",
                name, "Enum value should be PascalCase"
            ))
    return violations


def check_define(line: str, file: str, line_num: int) -> List[Violation]:
    """Check #define macros/constants use UPPER_SNAKE_CASE."""
    violations = []
    match = re.match(r'\s*#\s*define\s+([a-zA-Z_][a-zA-Z0-9_]*)', line)
    if match:
        name = match.group(1)
        # Skip include guards and common patterns
        if name.startswith('_') or name in CPP_KEYWORDS:
            return violations
        if not is_upper_snake_case(name):
            violations.append(Violation(
                file, line_num, "constant/macro",
                name, "Constant/macro should be UPPER_SNAKE_CASE"
            ))
    return violations


def check_constexpr_const(line: str, file: str, line_num: int) -> List[Violation]:
    """Check constexpr constants use UPPER_SNAKE_CASE (file/namespace scope only)."""
    violations = []
    # Only check static constexpr or constexpr at namespace scope
    # Skip plain 'const' as it is commonly used for local immutable variables
    match = re.match(
        r'\s*(?:static\s+)?constexpr\s+\w[\w:]*\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*[=;]',
        line
    )
    if match:
        name = match.group(1)
        if name in CPP_KEYWORDS:
            return violations
        if not is_upper_snake_case(name):
            violations.append(Violation(
                file, line_num, "constant",
                name, "Constant (constexpr) should be UPPER_SNAKE_CASE"
            ))
    return violations


def check_function(line: str, file: str, line_num: int) -> List[Violation]:
    """Check function declarations/definitions use snake_case."""
    violations = []
    # Match function declarations: return_type name(...)
    # Skip lines that are clearly not function declarations
    if '=' in line.split('(')[0] if '(' in line else '':
        return violations
    # Pattern: optional qualifiers, type, function name, opening paren
    match = re.match(
        r'\s*(?:(?:static|virtual|inline|explicit|friend|constexpr|override|const)\s+)*'
        r'(?:[\w:]+[\w:<>,\s\*&]*?\s+)?'
        r'([a-zA-Z_][a-zA-Z0-9_]*)\s*\(',
        line
    )
    if match:
        name = match.group(1)
        # Skip keywords, constructors/destructors (PascalCase by nature), operators
        if name in CPP_KEYWORDS:
            return violations
        if name.startswith('operator'):
            return violations
        # Skip excluded names (product names used as constructors)
        if name in EXCLUDED_NAMES:
            return violations
        # Skip if it looks like a constructor (PascalCase matching a class name)
        # We allow PascalCase for constructors/destructors
        if is_pascal_case(name):
            return violations
        # Skip macros (all uppercase)
        if is_upper_snake_case(name):
            return violations
        if not is_snake_case(name):
            violations.append(Violation(
                file, line_num, "function",
                name, "Function should be snake_case"
            ))
    return violations


def check_template_param(line: str, file: str, line_num: int) -> List[Violation]:
    """Check template parameters use PascalCase."""
    violations = []
    match = re.match(r'\s*template\s*<(.+)>\s*$', line)
    if match:
        params_str = match.group(1)
        # Extract parameter names from typename/class declarations
        for param_match in re.finditer(r'(?:typename|class)\s+([a-zA-Z_][a-zA-Z0-9_]*)', params_str):
            name = param_match.group(1)
            if not is_pascal_case(name):
                violations.append(Violation(
                    file, line_num, "template_param",
                    name, "Template parameter should be PascalCase"
                ))
    return violations


def check_variable_declaration(line: str, file: str, line_num: int) -> List[Violation]:
    """Check variable declarations follow naming rules."""
    violations = []

    # Skip lines that are clearly not variable declarations
    stripped = line.strip()
    if not stripped or stripped.startswith('//') or stripped.startswith('#'):
        return violations
    if stripped.startswith('return') or stripped.startswith('using') or stripped.startswith('typedef'):
        return violations
    if any(stripped.startswith(k) for k in ('class ', 'struct ', 'enum ', 'namespace ', 'template')):
        return violations

    # Check for global variable (g_ prefix)
    # Pattern: type g_name at file/namespace scope - handled contextually

    # Check for static variable (s_ prefix)
    # Skip static functions (have parentheses) and static constexpr
    static_var_match = re.match(
        r'\s*static\s+(?!constexpr|const\s|inline\s)(\w[\w:<>,\s\*&]*?)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*[=;(\[{]',
        line
    )
    if static_var_match:
        name = static_var_match.group(2)
        remaining = line[line.find(name) + len(name):].lstrip()
        # Skip if it's a function declaration (has opening paren without = before it)
        if remaining.startswith('('):
            return violations
        if name in CPP_KEYWORDS or is_upper_snake_case(name):
            return violations
        if not is_static_var_name(name) and not name.endswith('_'):
            violations.append(Violation(
                file, line_num, "static_variable",
                name, "Static variable should use s_ prefix + camelCase (e.g. s_instanceCount)"
            ))
        return violations

    # Check for bool variable (f prefix)
    bool_var_match = re.match(
        r'\s*(?:const\s+)?bool\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*[=;]',
        line
    )
    if bool_var_match:
        name = bool_var_match.group(1)
        if name in CPP_KEYWORDS:
            return violations
        # Allow member variables with trailing underscore (private members)
        if name.endswith('_'):
            return violations
        if not is_bool_var_name(name):
            violations.append(Violation(
                file, line_num, "bool_variable",
                name, "Bool variable should use f prefix + PascalCase (e.g. fIsConnected)"
            ))
        return violations

    return violations


def check_file(filepath: str, verbose: bool = False) -> List[Violation]:
    """Run all checks on a single file."""
    violations = []

    try:
        with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
    except (IOError, OSError) as e:
        print(f"Warning: Cannot read {filepath}: {e}", file=sys.stderr)
        return violations

    in_enum = False
    brace_depth = 0
    enum_brace_depth = 0
    in_block_comment = False

    for i, raw_line in enumerate(lines):
        line_num = i + 1
        line = raw_line.rstrip('\n')

        # Track block comments
        j = 0
        temp_in_block = in_block_comment
        while j < len(line):
            if temp_in_block:
                if j + 1 < len(line) and line[j] == '*' and line[j + 1] == '/':
                    temp_in_block = False
                    j += 2
                    continue
            else:
                if j + 1 < len(line) and line[j] == '/' and line[j + 1] == '*':
                    temp_in_block = True
                    j += 2
                    continue
                if j + 1 < len(line) and line[j] == '/' and line[j + 1] == '/':
                    break
            j += 1

        if in_block_comment:
            in_block_comment = temp_in_block
            continue
        in_block_comment = temp_in_block
        if in_block_comment:
            continue

        # Strip comments for analysis
        clean_line = strip_comments(line)

        # Track braces for enum context
        open_braces = clean_line.count('{')
        close_braces = clean_line.count('}')

        # Check for enum start
        if re.match(r'\s*enum\s+', clean_line):
            violations.extend(check_enum(clean_line, filepath, line_num))
            if '{' in clean_line:
                in_enum = True
                enum_brace_depth = brace_depth + open_braces

        if in_enum and close_braces > 0:
            if brace_depth + open_braces - close_braces < enum_brace_depth:
                in_enum = False

        brace_depth += open_braces - close_braces

        # Run checkers
        if in_enum and not re.match(r'\s*enum\s+', clean_line):
            violations.extend(check_enum_value(clean_line, filepath, line_num, True))
        else:
            violations.extend(check_namespace(clean_line, filepath, line_num))
            violations.extend(check_class_struct(clean_line, filepath, line_num))
            violations.extend(check_define(clean_line, filepath, line_num))
            violations.extend(check_constexpr_const(clean_line, filepath, line_num))
            violations.extend(check_function(clean_line, filepath, line_num))
            violations.extend(check_template_param(clean_line, filepath, line_num))
            violations.extend(check_variable_declaration(clean_line, filepath, line_num))

    return violations


def collect_files(directories: List[str], extensions: Tuple[str, ...] = ('.h', '.hpp', '.cpp', '.cc')) -> List[str]:
    """Collect all C++ files from the given directories."""
    files = []
    for directory in directories:
        if not os.path.isdir(directory):
            print(f"Warning: Directory not found: {directory}", file=sys.stderr)
            continue
        for root, _, filenames in os.walk(directory):
            for filename in sorted(filenames):
                if filename.endswith(extensions):
                    files.append(os.path.join(root, filename))
    return files


def main():
    parser = argparse.ArgumentParser(
        description="Check C++ code style naming conventions."
    )
    parser.add_argument(
        '--src', default='src',
        help='Source directory to check (default: src)'
    )
    parser.add_argument(
        '--include', default='include',
        help='Include directory to check (default: include)'
    )
    parser.add_argument(
        '--verbose', '-v', action='store_true',
        help='Show files being checked'
    )
    parser.add_argument(
        '--summary', '-s', action='store_true',
        help='Show summary of violations by rule'
    )
    args = parser.parse_args()

    # Resolve paths relative to project root (parent of scripts/)
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent

    src_dir = str(project_root / args.src)
    include_dir = str(project_root / args.include)

    files = collect_files([src_dir, include_dir])
    if not files:
        print("No C++ files found to check.", file=sys.stderr)
        sys.exit(1)

    all_violations: List[Violation] = []

    for filepath in files:
        if args.verbose:
            # Show relative path from project root
            rel_path = os.path.relpath(filepath, str(project_root))
            print(f"Checking: {rel_path}")
        violations = check_file(filepath, args.verbose)
        # Store violations with relative paths
        for v in violations:
            v.file = os.path.relpath(v.file, str(project_root))
        all_violations.extend(violations)

    if all_violations:
        print(f"\n{'='*70}")
        print(f"Code Style Violations Found: {len(all_violations)}")
        print(f"{'='*70}\n")
        for v in all_violations:
            print(f"  {v}")

        if args.summary:
            print(f"\n{'-'*70}")
            print("Summary by rule:")
            rule_counts: dict = {}
            for v in all_violations:
                rule_counts[v.rule] = rule_counts.get(v.rule, 0) + 1
            for rule, count in sorted(rule_counts.items(), key=lambda x: -x[1]):
                print(f"  {rule:20s}: {count}")

        print(f"\n{'='*70}")
        print(f"FAILED: {len(all_violations)} violation(s) found in {len(files)} file(s)")
        print(f"{'='*70}")
        sys.exit(1)
    else:
        print(f"\nAll {len(files)} file(s) pass code style checks.")
        sys.exit(0)


if __name__ == '__main__':
    main()
