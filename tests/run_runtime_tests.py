#!/usr/bin/env python3
"""Run the same semantic regressions through the interpreter and native binaries."""
import argparse
import os
import shutil
import sys
from pathlib import Path
import subprocess
import tempfile


def program(body, result="عدد"):
    return f"دالة بداية(): {result} {{ {body} }}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interp", required=True)
    parser.add_argument("--native")
    args = parser.parse_args()
    # name, source, expected stdout, expected exit, diagnostic, rejected before execution
    cases = []

    def ok(name, body, code=0, output="", source=False):
        cases.append((name, body if source else program(body), output, code, "", False))

    def fail(name, body, diagnostic, compile_error=False, output="", source=False):
        cases.append((name, body if source else program(body), output, 1, diagnostic, compile_error))

    ok("negative_float", 'دع س = -1.5؛ إذا (س == -1.5) { أعد 0؛ } أعد 99؛')
    ok("wide_integer", 'دع س = 2147483648؛ إذا (س > 2147483647) { أعد 0؛ } أعد 99؛')
    ok("double_precision", 'دع س = 16777217.0؛ إذا (س != 16777216.0) { أعد 0؛ } أعد 99؛')
    ok("min_max", 'دع س = -9223372036854775808؛ دع ص = 9223372036854775807؛ إذا (س + ص == -1) { أعد 0؛ } أعد 99؛')
    ok("min_leading_zero", 'دع س = -09223372036854775808؛ إذا (س < -9223372036854775807) { أعد 0؛ } أعد 99؛')
    fail("nul_string", 'دع س = "a' + chr(0) + 'b"؛ أعد 0؛', "NUL", True)
    fail("null_index_effect", 'دالة أثر(): عدد { اطبع("BAD")؛ أعد 0؛ } دالة بداية(): عدد { دع س: [عدد] = فارغ؛ أعد س[أثر()]؛ }', "", source=True)
    fail("null_write_index_effect", 'دالة أثر(): عدد { اطبع("BAD")؛ أعد 0؛ } دالة بداية(): عدد { دع س: [عدد] = فارغ؛ س[أثر()] = 1؛ أعد 0؛ }', "", source=True)
    ok("arabic_min", 'دع س = -٩٢٢٣٣٧٢٠٣٦٨٥٤٧٧٥٨٠٨؛ إذا (س < -٩٢٢٣٣٧٢٠٣٦٨٥٤٧٧٥٨٠٧) { أعد 0؛ } أعد 99؛')
    ok("arabic_digits", 'أعد ١٢٣؛', 123)
    ok("exit_truncation", 'أعد 4294967297؛', 1)
    ok("exit_negative", 'أعد -1؛', 255)
    ok("bool_exit", 'دالة بداية(): منطقي { أعد صح؛ }', 1, source=True)
    ok("void_exit", 'دالة بداية() { أعد؛ }', source=True)
    ok("null_exit", 'دالة بداية(): فارغ { أعد فارغ؛ }', source=True)
    ok("print_status", 'أعد اطبع("status")؛', output="status\n")
    ok("bool_conversion", 'دع س: عدد = صح؛ إذا (س + 1 == 2) { أعد 0؛ } أعد 99؛')
    ok("float_conversion", 'دع س: عشري = 1؛ س = 3؛ إذا (س / 2 == 1.5) { أعد 0؛ } أعد 99؛')
    ok("call_return_conversion", 'دالة نصف(س: عشري): عشري { أعد س / 2؛ } دالة واحد(): عشري { أعد 1؛ } دالة بداية(): عدد { إذا (نصف(1) == 0.5 و واحد() / 2 == 0.5) { أعد 0؛ } أعد 99؛ }', source=True)
    ok("array_conversion", 'دع س = [1، 2.0]؛ س[1] = 3؛ إذا (س[0] / 2 == 0.5 و س[1] / 2 == 1.5) { أعد 0؛ } أعد 99؛')
    ok("struct_conversion", 'نوع بنية نقطة { س: عشري }؛ دالة بداية(): عدد { دع ن = نقطة { س: 1 }؛ إذا (ن.س / 2 == 0.5) { أعد 0؛ } أعد 99؛ }', source=True)
    ok("short_circuit", 'دالة أثر(): منطقي { اطبع("BAD")؛ أعد صح؛ } دالة بداية(): عدد { دع س = خطأ و أثر()؛ دع ص = صح أو أثر()؛ أعد 0؛ }', source=True)
    ok("short_circuit_fault", 'دع س = خطأ و (1 / 0 == 0)؛ دع ص = صح أو (1 / 0 == 0)؛ أعد 0؛')
    ok("nested_short_circuit", 'إذا ((صح و صح) أو (خطأ و صح)) { أعد 0؛ } أعد 99؛')
    ok("void_call", 'دالة foo() { أعد؛ } دالة بداية(): عدد { foo()؛ أعد 0؛ }', source=True)
    ok("void_value", 'دالة foo() { أعد؛ } دالة bar(): فارغ { أعد foo()؛ } دالة بداية(): عدد { دع س = foo()؛ إذا (س == bar()) { أعد 0؛ } أعد 99؛ }', source=True)
    fail("index_write_effect", 'دالة أثر(): عدد { اطبع("BAD")؛ أعد 7؛ } دالة بداية(): عدد { دع س = [1]؛ س[2] = أثر()؛ أعد 0؛ }', "bounds", source=True)
    ok("entry_call", 'دالة بداية(): عدد { إذا (خطأ) { أعد بداية()؛ } أعد 0؛ }', source=True)
    ok("symbol_collision", 'دالة main(): عدد { أعد 7؛ } دالة بداية(): عدد { أعد main()؛ }', 7, source=True)
    ok("string_equality", 'دع س = "نص"؛ إذا (س == "نص" و س != "آخر") { أعد 0؛ } أعد 99؛')
    ok("null_string_equality", 'دع س: نص = فارغ؛ إذا (س == فارغ و س != "نص") { أعد 0؛ } أعد 99؛')
    ok("array_identity", 'دع س = [1]؛ دع ص = س؛ دع ع = [1]؛ إذا (س == ص و س != ع و س != فارغ) { أعد 0؛ } أعد 99؛')
    ok("null_array_equality", 'دع س: [عدد] = فارغ؛ إذا (س == فارغ) { أعد 0؛ } أعد 99؛')
    ok("tiny_divisor", 'دع س = 0.0000000000001؛ إذا (1.0 / س > 0.0) { أعد 0؛ } أعد 99؛')
    ok("float_exact", 'إذا (0.0000000001 != 0.0) { أعد 0؛ } أعد 99؛')
    ok("nan_inequality", 'دع س = ' + '9' * 308 + '.0؛ دع ن = س * س - س * س؛ إذا (ن != ن) { أعد 0؛ } أعد 99؛')
    ok("constant_alias", 'ثابت س = [1]؛ دع ص = س؛ ص[0] = 7؛ أعد س[0]؛', 7)
    ok("constant_shadow", 'ثابت س = 1؛ { دع س = 2؛ س = 3؛ } أعد س؛', 1)
    for name, expression in (
        ("add", "9223372036854775807 + 1"),
        ("sub", "-9223372036854775808 - 1"),
        ("mul", "9223372036854775807 * 2"),
        ("mul_negative", "-9223372036854775808 * -1"),
        ("negate", "-(-9223372036854775808)"),
        ("division_overflow", "-9223372036854775808 / -1"),
    ):
        fail(name, f"أعد {expression}؛", "Integer overflow")
    fail("division_zero", "أعد 1 / 0؛", "Division by zero")
    fail("float_zero", "دع س = 1.0 / 0.0؛ أعد 0؛", "Division by zero")
    fail("partial_output", 'اطبع("before failure")؛ أعد 9223372036854775807 + 1؛', "Integer overflow", output="before failure\n")
    for name, body in (
        ("null_length", 'دع س: [عدد] = فارغ؛ أعد س.طول؛'),
        ("null_read", 'دع س: [عدد] = فارغ؛ أعد س[0]؛'),
        ("null_write", 'دع س: [عدد] = فارغ؛ س[0] = 1؛ أعد 0؛'),
        ("negative_index", 'دع س = [1]؛ أعد س[-1]؛'),
        ("upper_index", 'دع س = [1]؛ أعد س[1]؛'),
        ("write_bounds", 'دع س = [1]؛ س[1] = 0؛ أعد 0؛'),
        ("null_print", 'دع س: نص = فارغ؛ اطبع(س)؛ أعد 0؛'),
    ):
        fail(name, body, "")
    for name, body, diagnostic in (
        ("const_write", 'ثابت س = 1؛ س = 2؛ أعد س؛', "constant"),
        ("const_index", 'ثابت س = [1]؛ س[0] = 2؛ أعد 0؛', "constant"),
        ("literal_range", 'أعد 9223372036854775808؛', "out of range"),
        ("negative_literal_range", 'أعد -9223372036854775809؛', "out of range"),
        ("float_range", 'دع س = ' + '9' * 310 + '.0؛ أعد 0؛', "out of range"),
        ("struct_equality", 'نوع بنية نقطة { س: عدد }؛ دالة بداية(): عدد { دع س = نقطة { س: 1 }؛ دع ص = س == س؛ أعد 0؛ }', "Equality"),
        ("function_equality", 'دع س = بداية == بداية؛ أعد 0؛', "Equality"),
    ):
        fail(name, body, diagnostic, True, source=name == "struct_equality")
    fail("top_level", 'دع س = 7؛ دالة بداية(): عدد { أعد 0؛ }', "Top-level", True, source=True)
    fail("entry_parameters", 'دالة بداية(س: عدد): عدد { أعد س؛ }', "Entry function", True, source=True)
    fail("entry_result", 'دالة بداية(): نص { أعد "نص"؛ }', "Entry function", True, source=True)

    with tempfile.TemporaryDirectory(prefix="dhad-runtime-") as directory:
        root = Path(directory)
        # Import processing must not bypass the top-level restriction.
        (root / "badmodule.dh").write_text("دع س = 1؛", encoding="utf-8")
        fail("import_top_level", 'استورد badmodule؛ دالة بداية(): عدد { أعد 0؛ }', "Top-level", True, source=True)
        for name, source, output, code, diagnostic, rejected in cases:
            path = root / f"{name}.dh"
            path.write_text(source, encoding="utf-8")
            commands = [("interpreter", [args.interp, str(path)])]
            if args.native:
                binary = root / (name + ".exe")
                compiled = subprocess.run([args.native, str(path), "-o", str(binary)], capture_output=True, text=True, encoding="utf-8", timeout=30)
                if rejected:
                    assert compiled.returncode == 1 and diagnostic in compiled.stderr, (name, compiled)
                else:
                    assert compiled.returncode == 0, (name, compiled.stderr)
                    commands.append(("native", [str(binary)]))
            for mode, command in commands:
                result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=10)
                assert result.returncode & 255 == code, (name, mode, result.returncode, result.stderr)
                assert result.stdout == output, (name, mode, result.stdout, output)
                if rejected or code == 1 and (diagnostic or name.startswith("null") or "index" in name or name == "write_bounds"):
                    assert result.stderr and diagnostic in result.stderr, (name, mode, result.stderr, diagnostic)
                else:
                    assert not result.stderr, (name, mode, result.stderr)
        if args.native:
            # Downloaded compilers must discover Clang on the consumer's PATH,
            # and an explicit override must take precedence over that PATH.
            unsupported = [
                program("دع س: عدد | نص = 1؛ أعد 0؛"),
                "دالة foo(): عدد | نص { أعد 1؛ } " + program("أعد 0؛"),
                "دالة foo(س: عدد | نص) { أعد؛ } " + program("foo(1)؛ أعد 0؛"),
            ]
            for source in unsupported:
                path = root / "unsupported.dh"
                path.write_text(source, encoding="utf-8")
                result = subprocess.run([args.native, str(path), "--emit-ir"],
                                        capture_output=True, text=True, encoding="utf-8", timeout=30)
                assert result.returncode == 1 and result.stderr and not result.stdout, result
            real_clang = shutil.which("clang")
            assert real_clang, "native tests require Clang"
            tools = root / "tools"
            tools.mkdir()
            marker = tools / "called"
            shim = tools / "clang"
            shim.write_text(f"#!{sys.executable}\nimport os, sys\n"
                            f"open({str(marker)!r}, 'w').write('called')\n"
                            f"os.execv({real_clang!r}, [{real_clang!r}, *sys.argv[1:]])\n")
            shim.chmod(0o755)
            smoke = root / "compiler_lookup.dh"
            smoke.write_text(program("أعد 0؛"), encoding="utf-8")
            for override in (False, True):
                marker.unlink(missing_ok=True)
                environment = dict(os.environ)
                environment.pop("DHAD_CLANG", None)
                if override:
                    environment["DHAD_CLANG"] = str(shim)
                else:
                    environment["PATH"] = str(tools) + os.pathsep + environment.get("PATH", "")
                result = subprocess.run([args.native, str(smoke), "-o", str(root / "lookup")],
                                        env=environment, capture_output=True, text=True, encoding="utf-8", timeout=30)
                assert result.returncode == 0 and marker.exists(), (override, result.stderr)
    print(f"{len(cases)} runtime regression cases passed" + (" in both engines" if args.native else " in interpreter"))


if __name__ == "__main__":
    main()
