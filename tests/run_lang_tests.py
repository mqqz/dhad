#!/usr/bin/env python3
"""Unified golden tests for lexer and parser modes."""

import argparse
import sys
from pathlib import Path

from harness import GoldenCase, run_golden_cases

REPO_ROOT = Path(__file__).resolve().parents[1]


def example_display_path(example: Path) -> str:
  try:
    return str(example.resolve().relative_to(REPO_ROOT))
  except ValueError:
    return example.name


def parser_normalizer_factory(example: Path):
  def _normalizer(text: str) -> str:
    normalized = text.replace("\r\n", "\n")
    trailing_newline = normalized.endswith("\n")
    lines = normalized.splitlines()
    if lines and lines[0].startswith("Parse succeeded:"):
      lines[0] = f"Parse succeeded: {example_display_path(example)}"
    rebuilt = "\n".join(lines)
    if trailing_newline:
      rebuilt += "\n"
    return rebuilt

  return _normalizer


def parse_args():
  parser = argparse.ArgumentParser(description="Run lexer/parser golden tests.")
  _= parser.add_argument("--driver", required=True, help="Path to lang_driver executable.")
  _= parser.add_argument(
      "--case",
      action="append",
      nargs=3,
      metavar=("MODE", "EXAMPLE", "EXPECTED"),
      help="Mode (lexer|parser), example source, expected output.")
  args = parser.parse_args()
  if not args.case:
    parser.error("At least one --case is required")
  return args


def main():
  args = parse_args()
  cases = []
  for mode, example, expected in args.case:
    example_path = Path(example)
    extra = ("--mode", mode)
    normalizer = parser_normalizer_factory(example_path) if mode == "parser" else None
    label = f"{mode}:{example}"
    cases.append(
        GoldenCase(
            example=example_path,
            expected=Path(expected),
            label=label,
            normalizer=normalizer,
            extra_args=extra,
        ))
  failures = run_golden_cases(Path(args.driver), cases)
  if failures:
    sys.stderr.write("\n".join(failures) + "\n")
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
