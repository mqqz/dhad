#!/usr/bin/env python3
"""Serve a preassembled playground and exercise its real WASM in Chromium."""

from __future__ import annotations

import argparse
import functools
import http.server
import threading
from pathlib import Path


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site-dir", type=Path, required=True)
    args = parser.parse_args()
    site_dir = args.site_dir.resolve()
    for name in ("index.html", "worker.js", "build/dhad_web.js", "build/dhad_web.wasm"):
        if not (site_dir / name).is_file():
            parser.error(f"missing site artifact: {site_dir / name}")

    from playwright.sync_api import sync_playwright

    handler = functools.partial(QuietHandler, directory=str(site_dir))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            page = browser.new_page()
            page.set_default_timeout(15000)
            errors: list[str] = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(f"http://127.0.0.1:{server.server_port}/")
            page.wait_for_function("document.querySelector('#source').value.length > 0")

            # A separate real worker exposes exit codes as well as UI-visible output.
            init = page.evaluate("""async () => {
              const worker = new Worker('./worker.js');
              let next = 0;
              const pending = new Map();
              window.testRequest = (type, source = '') => new Promise((resolve, reject) => {
                const id = ++next;
                const timer = setTimeout(() => {
                  pending.delete(id);
                  reject(new Error('Browser test worker timed out'));
                }, 10000);
                pending.set(id, { resolve, reject, timer });
                worker.postMessage({ id, type, source, modulePath: './build/dhad_web.js' });
              });
              worker.onmessage = ({ data }) => {
                const entry = pending.get(data.id);
                if (!entry) return;
                pending.delete(data.id);
                clearTimeout(entry.timer);
                if (data.ok) entry.resolve(data);
                else entry.reject(new Error(data.error));
              };
              worker.onerror = (event) => {
                for (const entry of pending.values()) {
                  clearTimeout(entry.timer);
                  entry.reject(new Error(event.message));
                }
                pending.clear();
              };
              return await window.testRequest('init');
            }""")
            assert init["ok"] is True

            def request(kind: str, source: str) -> dict:
                return page.evaluate("([type, source]) => window.testRequest(type, source)", [kind, source])

            hello = 'دالة بداية(): عدد { اطبع("مرحبا")؛ أعد 0؛ }'
            parsed = request("parse", hello)
            assert parsed["success"] and "Program" in parsed["ast"], parsed
            cases = [
                (hello, 0, "مرحبا\n"),
                ('دالة بداية(): عدد { اطبع("error: literal output")؛ أعد 0؛ }', 0, "error: literal output\n"),
                ('دالة بداية(): عدد { دع س = 2147483648؛ إذا (س > 2147483647) { أعد 37؛ } أعد 1؛ }', 37, ""),
                ('دالة بداية(): عدد { دع س: عشري = 1؛ إذا (س / 2 == 0.5) { أعد 38؛ } أعد 1؛ }', 38, ""),
                ('دالة بداية(): عدد { دع س: عشري = -1.5؛ إذا (س == -1.5) { أعد 39؛ } أعد 1؛ }', 39, ""),
                ('دالة أثر(): منطقي { اطبع("unexpected")؛ أعد صح؛ } دالة بداية(): عدد { دع س = خطأ و أثر()؛ دع ص = صح أو أثر()؛ أعد 0؛ }', 0, ""),
                ('دالة بداية(): عدد { إذا ("نص" == "نص") { أعد 40؛ } أعد 1؛ }', 40, ""),
            ]
            for source, exit_code, stdout in cases:
                result = request("run", source)
                assert result["success"] and result["exitCode"] == exit_code, result
                assert result["stdout"] == stdout and result["stderr"] == "", result

            type_error = request("run", 'دالة بداية(): عدد { دع س: عدد = "نص"؛ أعد 0؛ }')
            assert not type_error["success"] and type_error["stderr"], type_error
            overflow = 'دالة بداية(): عدد { اطبع("before failure")؛ دع س = 9223372036854775807؛ أعد س + 1؛ }'
            failed = request("run", overflow)
            assert not failed["success"] and "overflow" in failed["stderr"].lower(), failed
            assert failed["stdout"] == "before failure\n", failed
            for source in (
                'دالة بداية(): عدد { دع س: [عدد] = فارغ؛ أعد س.طول؛ }',
                'دالة بداية(): عدد { دع س = [1]؛ أعد س[2]؛ }',
            ):
                failed = request("run", source)
                assert not failed["success"] and failed["stderr"], failed

            # Repeated sizeable inputs exercise the real heap; precise ownership
            # assertions are in web_worker_test.cjs, independent of allocator reuse.
            large = 'دالة بداية(): عدد { اطبع("' + "ض" * 8192 + '")؛ أعد 0؛ }'
            for _ in range(25):
                assert request("parse", large)["success"]
                result = request("run", large)
                assert result["success"] and result["stdout"] == "ض" * 8192 + "\n"

            def click(kind: str, source: str) -> tuple[str, str, str]:
                page.locator("#source").fill(source)
                page.locator(f"#{kind}Btn").click()
                page.wait_for_function("!document.querySelector('#runBtn').disabled && !document.querySelector('#parseBtn').disabled")
                return tuple(page.locator(selector).text_content() or "" for selector in ("#output", "#diagnostics", "#ast"))

            output, diagnostics, _ = click("run", cases[1][0])
            assert output == "error: literal output\n" and diagnostics == "", (output, diagnostics)
            output, diagnostics, _ = click("run", overflow)
            assert output == "before failure\n" and "overflow" in diagnostics.lower(), (output, diagnostics)
            _, diagnostics, ast = click("parse", hello)
            assert diagnostics == "" and "Program" in ast, (diagnostics, ast)
            _, diagnostics, _ = click("run", 'دالة بداية(): عدد { طالما (صح) { } أعد 0؛ }')
            assert "timed out" in diagnostics.lower(), diagnostics
            output, diagnostics, _ = click("run", hello)
            assert output == "مرحبا\n" and diagnostics == "", (output, diagnostics)
            assert not errors, errors
            browser.close()
            print("Real WASM worker, numeric regressions, UI diagnostics, repeated inputs, and timeout recovery passed.")
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
