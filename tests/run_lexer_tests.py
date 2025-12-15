#!/usr/bin/env python3
"""Smoke tests for the lexer binary."""

import argparse
import sys
from pathlib import Path

from harness import GoldenCase, run_golden_cases


def parse_args():
  parser = argparse.ArgumentParser(description="Run lexer smoke tests.")
  parser.add_argument("--tokenizer", required=True, help="Path to tokenizer executable.")
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
  cases = [
      GoldenCase(example=Path(example), expected=Path(expected))
      for example, expected in args.case
  ]
  failures = run_golden_cases(Path(args.tokenizer), cases)
  if failures:
    sys.stderr.write("\n".join(failures) + "\n")
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
