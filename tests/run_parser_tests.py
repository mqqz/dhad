#!/usr/bin/env python3
"""Smoke tests for the parser binary."""

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
    normalised = text.replace("\r\n", "\n")
    trailing_newline = normalised.endswith("\n")
    lines = normalised.splitlines()
    if lines and lines[0].startswith("Parse succeeded:"):
      lines[0] = f"Parse succeeded: {example_display_path(example)}"
    rebuilt = "\n".join(lines)
    if trailing_newline:
      rebuilt += "\n"
    return rebuilt

  return _normalizer


def parse_args():
  parser = argparse.ArgumentParser(description="Run parser smoke tests.")
  parser.add_argument("--parser", required=True, help="Path to parser executable.")
  parser.add_argument(
      "--case",
      action="append",
      nargs=2,
      metavar=("EXAMPLE", "EXPECTED"),
      help="Example source path and expected output path.")
  args = parser.parse_args()
  if not args.case:
    parser.error("At least one --case must be provided")
  return args


def main():
  args = parse_args()

  cases = []
  for example, expected in args.case:
    example_path = Path(example)
    cases.append(
        GoldenCase(
            example=example_path,
            expected=Path(expected),
            label=example,
            normalizer=parser_normalizer_factory(example_path),
        ))

  failures = run_golden_cases(Path(args.parser), cases)
  if failures:
    sys.stderr.write("\n".join(failures) + "\n")
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
