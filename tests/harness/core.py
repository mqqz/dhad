"""Core helpers shared by the custom Python test scripts."""

from __future__ import annotations

import dataclasses
import difflib
import subprocess
from collections.abc import Callable, Iterable, Sequence
from pathlib import Path
from typing import List, Union

from subprocess import CompletedProcess


class CommandError(RuntimeError):
  """Raised when a subprocess exits with an unexpected return code."""

  cmd: list[str]
  result: CompletedProcess[Union[str, bytes]]

  def __init__(self, cmd: Sequence[str],
               result: CompletedProcess[Union[str, bytes]]):
    self.cmd = list(cmd)
    self.result = result
    message = (
        f"Command failed ({result.returncode}): {' '.join(self.cmd)}\n"
        f"stdout:\n{_as_text(result.stdout)}\n"
        f"stderr:\n{_as_text(result.stderr)}\n"
    )
    super().__init__(message)


def _as_text(data: Union[str, bytes, None]) -> str:
  if data is None:
    return ""
  if isinstance(data, str):
    return data
  return data.decode("utf-8", errors="ignore")


def run_command(cmd: Sequence[str],
                *,
                input_data: bytes | str | None = None,
                text: bool = True,
                allowed_returncodes: Iterable[int] = (0,)
               ) -> CompletedProcess[Union[str, bytes]]:
  """Run a subprocess and raise CommandError on failure."""
  result = subprocess.run(
      cmd,
      input=input_data,
      capture_output=True,
      text=text,
      check=False,
  )
  if result.returncode not in allowed_returncodes:
    raise CommandError(cmd, result)
  return result


def _default_normalizer(text: str) -> str:
  return text.replace("\r\n", "\n")


@dataclasses.dataclass(frozen=True)
class GoldenCase:
  """Describes a golden-file comparison."""

  example: Path
  expected: Path
  label: str | None = None
  normalizer: Callable[[str], str] | None = None


def _default_command(tool: Path, case: GoldenCase) -> Sequence[str]:
  return [str(tool), str(case.example)]


def run_golden_cases(
    tool: Path,
    cases: Sequence[GoldenCase],
    *,
    command_builder: Callable[[Path, GoldenCase], Sequence[str]] | None = None,
) -> List[str]:
  """Run a CLI tool against multiple inputs and compare to golden outputs.

  Returns:
    A list of failure messages (empty when every case matches).
  """
  tool_path = Path(tool)
  failures: List[str] = []

  if not tool_path.is_file():
    return [f"Tool not found: {tool_path}"]

  builder = command_builder or _default_command

  for case in cases:
    example = case.example
    expected = case.expected
    label = case.label or str(example)
    if not example.is_file():
      failures.append(f"Example not found: {example}")
      continue
    if not expected.is_file():
      failures.append(f"Expected output not found: {expected}")
      continue

    try:
      result = run_command(builder(tool_path, case))
    except CommandError as exc:
      failures.append(str(exc))
      continue

    expected_text = expected.read_text(encoding="utf-8")
    normalizer = case.normalizer or _default_normalizer
    actual_text = normalizer(result.stdout)
    expected_text = normalizer(expected_text)

    if actual_text != expected_text:
      diff = "".join(
          difflib.unified_diff(
              expected_text.splitlines(keepends=True),
              actual_text.splitlines(keepends=True),
              fromfile="expected",
              tofile="actual",
          ))
      failures.append(f"Output mismatch for {label}\n{diff}")

  return failures
