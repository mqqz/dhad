#!/usr/bin/env python3
"""Golden tests for dhad compiler diagnostics."""

import argparse
import sys
from pathlib import Path

from harness import CommandError, run_command

REPO_ROOT = Path(__file__).resolve().parents[1]


def example_display_path(example: Path) -> str:
  try:
    return str(example.resolve().relative_to(REPO_ROOT))
  except ValueError:
    return example.name


def parse_args():
  parser = argparse.ArgumentParser(description="Run dhad diagnostics tests.")
  _ = parser.add_argument("--driver", required=True, help="Path to dhad executable.")
  _ = parser.add_argument(
      "--case",
      action="append",
      nargs=2,
      metavar=("EXAMPLE", "EXPECTED"),
      help="Example source and expected stderr output.")
  args = parser.parse_args()
  if not args.case:
    parser.error("At least one --case is required")
  return args


def normalize(text: str, example: Path) -> str:
  normalized = text.replace("\r\n", "\n")
  return normalized.replace(str(example.resolve()), example_display_path(example))


def main():
  args = parse_args()
  driver = Path(args.driver)
  if not driver.is_file():
    sys.stderr.write(f"Tool not found: {driver}\n")
    return 1

  failures = []
  for example, expected in args.case:
    example_path = Path(example)
    expected_path = Path(expected)
    label = f"diagnostic:{example}"
    if not example_path.is_file():
      failures.append(f"Example not found: {example_path}")
      continue
    if not expected_path.is_file():
      failures.append(f"Expected output not found: {expected_path}")
      continue

    try:
      result = run_command([str(driver), str(example_path)], allowed_returncodes=(1,))
    except CommandError as exc:
      failures.append(str(exc))
      continue

    expected_text = normalize(expected_path.read_text(encoding="utf-8"), example_path)
    actual_text = normalize(result.stderr or "", example_path)
    if actual_text != expected_text:
      failures.append(
          f"Output mismatch for {label}\nExpected:\n{expected_text}\nActual:\n{actual_text}\n")

  if failures:
    sys.stderr.write("\n".join(failures) + "\n")
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
