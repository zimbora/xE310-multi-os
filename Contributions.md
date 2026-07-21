# Contributions

> **Contribution policy**
>
> - Create a new branch before starting any development work.
> - Contributions are accepted only through Pull Requests (PRs).

## Overview

This document defines contribution workflow for this repository, including commit message rules, validation checks, and changelog generation.

## Conventional Commits

This project follows the [Conventional Commits](https://www.conventionalcommits.org/) specification for commit messages.

### Format

```text
<type>(<optional scope>): <description>
```

### Types

| Type       | When to use |
|------------|-------------|
| `feat`     | A new feature or capability visible to consumers of the library |
| `fix`      | A bug fix |
| `refactor` | Code change that is neither a feature nor a bug fix (restructuring, renaming) |
| `perf`     | A change that improves performance |
| `test`     | Adding or correcting tests with no production code change |
| `docs`     | Documentation only (README, Doxygen comments, inline comments) |
| `style`    | Formatting and whitespace changes with no logic change |
| `chore`    | Build system, CI config, dependency bumps with no production code change |
| `ci`       | Changes to GitHub Actions workflows only |
| `revert`   | Reverts a previous commit (reference the reverted SHA in the footer) |

## Validation Checklist

Before considering a task complete, run static analysis checks when code changes affect C++ sources or headers.

Required checks:

```bash
clang-format --dry-run --Werror src/*.cpp src/hal/*.cpp include/modem/*.h include/hal/*.h
clang-tidy --extra-arg="-Iinclude" --extra-arg="-std=c++17" --warnings-as-errors="*" src/*.cpp
cppcheck --enable=all --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=normalCheckLevelMaxBranches --suppress=checkersReport --error-exitcode=1 --std=c++17 -I include src/ include/
```

If any check cannot be run, explicitly state why and what remains unvalidated.

## Changelog Generation

A changelog is automatically generated when a tag is pushed (via the `changelog.yml` workflow).

To generate a changelog locally:

```bash
# Changes since the last tag
bash scripts/generate_changelog.sh

# Changes since a specific tag
bash scripts/generate_changelog.sh v1.0.0

# Changes between two tags
bash scripts/generate_changelog.sh v1.0.0 v2.0.0
```
