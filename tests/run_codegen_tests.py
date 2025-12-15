#!/usr/bin/env python3
import argparse
import sys
import tempfile
from pathlib import Path

from harness import CommandError, run_command


def parse_args():
  parser = argparse.ArgumentParser(
      description="Run codegen_ex1, verify IR, execute with lli.")
  parser.add_argument("--codegen", required=True, help="Path to codegen_ex1.")
  parser.add_argument("--example", required=True, help="Path to example source.")
  parser.add_argument("--opt", required=True, help="Path to opt binary.")
  parser.add_argument("--lli", required=True, help="Path to lli binary.")
  parser.add_argument("--expected", default="س أقل\n",
                      help="Expected program output.")
  return parser.parse_args()


def main():
  args = parse_args()

  example_path = Path(args.example)
  if not example_path.is_file():
    sys.stderr.write(f"Example file not found: {example_path}\n")
    return 1

  try:
    codegen_result = run_command([args.codegen, str(example_path)], text=False)
  except CommandError as exc:
    sys.stderr.write(str(exc))
    return 1

  ir_bytes = codegen_result.stdout

  with tempfile.NamedTemporaryFile(mode="wb", suffix=".ll", delete=False) as tmp:
    tmp.write(ir_bytes)
    ir_path = tmp.name

  try:
    try:
      run_command([args.opt, "-passes=verify", "-disable-output", ir_path])
      lli_result = run_command(
          [args.lli, ir_path], text=False, allowed_returncodes=(0, 1, 5))
    except CommandError as exc:
      sys.stderr.write(str(exc))
      return 1
  finally:
    Path(ir_path).unlink(missing_ok=True)

  output = lli_result.stdout.decode("utf-8", errors="ignore")
  if output != args.expected:
    sys.stderr.write(
        f"Unexpected output.\nExpected:\n{args.expected}\nActual:\n{output}\n")
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
