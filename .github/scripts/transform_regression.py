#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path


COMMANDS = (
    ("ROLZ", ("-type=ROLZ", "-noperf")),
    ("SRT", ("-type=SRT", "-noperf")),
    ("RANK", ("-type=RANK", "-noperf")),
    ("all", ("-type=all", "-noperf")),
)


def print_tail(path, lines=80):
    try:
        content = path.read_text(errors="replace").splitlines()
    except OSError as exc:
        print(f"Could not read log {path}: {exc}")
        return

    tail = content[-lines:]

    if tail:
        print(f"--- tail of {path.name} ---")
        print("\n".join(tail))
        print("--- end tail ---")


def run_case(executable, name, args, timeout):
    log_file = Path(tempfile.gettempdir()) / f"kanzi-transform-{name}.log"
    command = [str(executable), *args]
    print("+ " + " ".join(command), flush=True)

    with log_file.open("w", encoding="utf-8", errors="replace") as log:
        try:
            result = subprocess.run(
                command,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=timeout,
                check=False,
            )
            return result.returncode, log_file
        except subprocess.TimeoutExpired:
            log.write(f"\nTIMEOUT after {timeout} seconds\n")
            return 124, log_file


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--expect", choices=("pass", "fail"), required=True)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()

    executable = args.executable.resolve()

    if not executable.is_file():
        raise SystemExit(f"Missing test executable: {executable}")

    failures = []

    for name, command_args in COMMANDS:
        code, log_file = run_case(executable, name, command_args, args.timeout)
        print(f"{name}: exit {code}, log {log_file}", flush=True)

        if code != 0:
            failures.append((name, code, log_file))
            print_tail(log_file)

    if args.expect == "pass":
        if failures:
            failed = ", ".join(f"{name}={code}" for name, code, _ in failures)
            raise SystemExit(f"Expected all transform cases to pass, got {failed}")

        print("All transform regression cases passed", flush=True)
        return

    if not failures:
        raise SystemExit("Expected the baseline to reproduce at least one transform failure")

    failed = ", ".join(f"{name}={code}" for name, code, _ in failures)
    print(f"Baseline reproduced transform failure(s): {failed}", flush=True)


if __name__ == "__main__":
    main()
