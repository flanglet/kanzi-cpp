#!/usr/bin/env python3

import argparse
import hashlib
import shutil
import shlex
import subprocess
import sys
from pathlib import Path


LANG_TO_COMMAND = {
    "cpp": "/Users/flanglet/ws/kanzi-cpp/bin/kanzi",
    "java": "java -jar /Users/flanglet/ws/kanzi/java/target/kanzi-2.5.0.jar",
    "go": "/Users/flanglet/ws/kanzi-go/v2/app/Kanzi",
}


def md5_file(path: Path) -> str:
    hsh = hashlib.md5()

    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)

            if not chunk:
                break

            hsh.update(chunk)

    return hsh.hexdigest()


def md5_dir(path: Path) -> str:
    hsh = hashlib.md5()

    files = [p for p in path.rglob("*") if p.is_file()]
    files.sort(key=lambda p: str(p.relative_to(path)))

    for file_path in files:
        rel = str(file_path.relative_to(path)).replace("\\", "/")
        hsh.update(rel.encode("utf-8"))
        hsh.update(b"\0")

        with file_path.open("rb") as f:
            while True:
                chunk = f.read(1024 * 1024)

                if not chunk:
                    break

                hsh.update(chunk)

    return hsh.hexdigest()


def list_relative_files(path: Path) -> list[Path]:
    files = [p.relative_to(path) for p in path.rglob("*") if p.is_file()]
    files.sort(key=lambda p: str(p).replace("\\", "/"))
    return files


def clean_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def run_command(argv: list[str]) -> None:
    print("Running:", " ".join(shlex.quote(x) for x in argv))
    lines: list[str] = []

    with subprocess.Popen(
        argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    ) as proc:
        assert proc.stdout is not None

        for line in proc.stdout:
            lines.append(line)
            stripped = line.lstrip()

            if stripped.startswith("Compressed") or stripped.startswith("Decompressed"):
                print(stripped.rstrip("\n"))

        rc = proc.wait()

    if rc != 0:
        if lines:
            print("Command output:", file=sys.stderr)
            for line in lines:
                print(line.rstrip("\n"), file=sys.stderr)
        raise subprocess.CalledProcessError(rc, argv)


def resolve_command(language: str | None, command: str | None) -> str:
    if language is not None:
        return LANG_TO_COMMAND[language]

    if command is None or not command.strip():
        raise ValueError(
            "Empty command: provide --language (cpp|java|go) or a non-empty --command"
        )

    return command.strip()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Test compression/decompression round trip with MD5 validation."
    )
    parser.add_argument("level", help="Compression level")
    parser.add_argument("input_path", help="Input file or directory path")
    parser.add_argument(
        "--block-size",
        dest="block_size",
        default=None,
        help="Optional block size (passed to -b)",
    )
    parser.add_argument(
        "--command",
        default=None,
        help="Kanzi command to run (ignored when --language is provided)",
    )
    parser.add_argument(
        "--language",
        choices=sorted(LANG_TO_COMMAND.keys()),
        default=None,
        help="Select command automatically based on language",
    )
    parser.add_argument(
        "--verbose",
        default="1",
        help="Kanzi verbosity level (default: 1)",
    )
    parser.add_argument(
        "--show-only-mismatch",
        action="store_true",
        help="For directory input, only display files with checksum mismatch",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    input_path = Path(args.input_path).resolve()

    if not input_path.exists():
        print(f"Error: input path does not exist: {input_path}", file=sys.stderr)
        return 2

    try:
        command = resolve_command(args.language, args.command)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    base_cmd = shlex.split(command)
    tmp_root = Path("/tmp")

    try:
        if input_path.is_dir():
            out_path = tmp_root / "out"
            bak_path = tmp_root / "bak"
            clean_dir(out_path)
            clean_dir(bak_path)
            cmp_input = input_path
            cmp_output = out_path
            dcmp_input = out_path
            dcmp_output = bak_path
            cmp_target = input_path
            dcmp_target = bak_path
        else:
            out_path = tmp_root / (input_path.name + ".knz")
            bak_path = tmp_root / (input_path.name + ".bak")

            if out_path.exists():
                out_path.unlink()

            if bak_path.exists():
                bak_path.unlink()

            cmp_input = input_path
            cmp_output = out_path
            dcmp_input = out_path
            dcmp_output = bak_path
            cmp_target = input_path
            dcmp_target = bak_path

        compress_cmd = (
            base_cmd
            + [
                "-c",
                "-i",
                str(cmp_input),
                "-o",
                str(cmp_output),
                "-l",
                str(args.level),
                "-f",
                "-v",
                str(args.verbose),
            ]
        )

        if args.block_size is not None:
            compress_cmd += ["-b", str(args.block_size)]

        decompress_cmd = (
            base_cmd
            + [
                "-d",
                "-i",
                str(dcmp_input),
                "-o",
                str(dcmp_output),
                "-f",
                "-v",
                str(args.verbose),
            ]
        )

        run_command(compress_cmd)
        run_command(decompress_cmd)

        if cmp_target.is_dir():
            orig_files = set(list_relative_files(cmp_target))
            dec_files = set(list_relative_files(dcmp_target))
            all_files = sorted(orig_files | dec_files, key=lambda p: str(p).replace("\\", "/"))
            all_match = True

            for rel in all_files:
                rel_str = str(rel).replace("\\", "/")
                orig_abs = cmp_target / rel
                dec_abs = dcmp_target / rel

                if rel not in orig_files:
                    all_match = False
                    print(f"[MISMATCH] {rel_str}: missing in original, present in decompressed")
                    continue

                if rel not in dec_files:
                    all_match = False
                    print(f"[MISMATCH] {rel_str}: present in original, missing in decompressed")
                    continue

                md5_original = md5_file(orig_abs)
                md5_roundtrip = md5_file(dec_abs)
                status = "OK" if md5_original == md5_roundtrip else "MISMATCH"

                if status == "MISMATCH":
                    all_match = False

                if (not args.show_only_mismatch) or status == "MISMATCH":
                    print(
                        f"[{status}] {rel_str}: Original MD5={md5_original} "
                        f"Decompressed MD5={md5_roundtrip}"
                    )

            print("Success" if all_match else "Failure")
            return 0 if all_match else 1
        else:
            md5_original = md5_file(cmp_target)
            md5_roundtrip = md5_file(dcmp_target)
            match = md5_original == md5_roundtrip

            if (not args.show_only_mismatch) or (not match):
                status = "OK" if match else "MISMATCH"
                print(
                    f"[{status}] {cmp_target.name}: Original MD5={md5_original} "
                    f"Decompressed MD5={md5_roundtrip}"
                )

            print("Success" if match else "Failure")
            return 0 if match else 1
    except subprocess.CalledProcessError as exc:
        print(f"Error: command failed with exit code {exc.returncode}", file=sys.stderr)
        return exc.returncode
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
