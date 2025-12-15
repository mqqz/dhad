#!/usr/bin/env python3
"""Wrapper around ctest that archives logs under tests/.logs."""

from __future__ import annotations

import argparse
import datetime
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description="Run ctest and archive logs.")
  parser.add_argument("--build-dir", default="build", help="CMake build directory.")
  parser.add_argument(
      "--logs-dir", default="tests/.logs", help="Directory where logs should be stored.")
  parser.add_argument("--ctest", default="ctest", help="Path to ctest executable.")
  parser.add_argument(
      "--ctest-args",
      default="--output-on-failure",
      help="Additional arguments to pass to ctest.")
  return parser.parse_args()


def stream_and_capture(cmd: list[str], cwd: Path, log_file: Path) -> int:
  with log_file.open("w", encoding="utf-8") as log:
    process = subprocess.Popen(
        cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    assert process.stdout is not None
    for line in process.stdout:
      sys.stdout.write(line)
      log.write(line)
    return process.wait()


def main() -> int:
  args = parse_args()
  build_dir = Path(args.build_dir).resolve()
  logs_dir = Path(args.logs_dir).resolve()
  logs_dir.mkdir(parents=True, exist_ok=True)

  timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
  run_dir = logs_dir / timestamp
  run_dir.mkdir(parents=True, exist_ok=False)
  log_file = run_dir / "ctest.log"

  ctest_cmd = [args.ctest]
  if args.ctest_args:
    ctest_cmd.extend(shlex.split(args.ctest_args))

  code = stream_and_capture(ctest_cmd, build_dir, log_file)

  testing_dir = build_dir / "Testing"
  if testing_dir.exists():
    dest = run_dir / "Testing"
    shutil.move(str(testing_dir), dest)

  latest_link = logs_dir / "latest"
  try:
    if latest_link.exists() or latest_link.is_symlink():
      latest_link.unlink()
    latest_link.symlink_to(run_dir, target_is_directory=True)
  except OSError:
    pass

  return code


if __name__ == "__main__":
  raise SystemExit(main())
