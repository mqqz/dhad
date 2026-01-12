#!/usr/bin/env python3
"""Golden tests for dhad --emit-ir output."""

import argparse
import sys
from pathlib import Path

from harness import CommandError, run_command


def parse_args():
  parser = argparse.ArgumentParser(description="Run dhad IR emission tests.")
  _ = parser.add_argument("--driver", required=True, help="Path to dhad executable.")
  _ = parser.add_argument(
      "--case",
      action="append",
      nargs=2,
      metavar=("EXAMPLE", "EXPECTED"),
      help="Example source and expected substrings file.")
  args = parser.parse_args()
  if not args.case:
    parser.error("At least one --case is required")
  return args


def normalize(text: str) -> str:
  return text.replace("\r\n", "\n")


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
    label = f"emit-ir:{example}"
    if not example_path.is_file():
      failures.append(f"Example not found: {example_path}")
      continue
    if not expected_path.is_file():
      failures.append(f"Expected output not found: {expected_path}")
      continue

    try:
      result = run_command([str(driver), str(example_path), "--emit-ir"])
    except CommandError as exc:
      failures.append(str(exc))
      continue

    output = normalize(result.stdout or "")
    required = [line.strip() for line in expected_path.read_text(encoding="utf-8").splitlines()]
    required = [line for line in required if line]
    missing = [snippet for snippet in required if snippet not in output]
    if missing:
      failures.append(
          f"Output mismatch for {label}\nMissing:\n" + "\n".join(missing) + "\n")

  if failures:
    sys.stderr.write("\n".join(failures) + "\n")
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
