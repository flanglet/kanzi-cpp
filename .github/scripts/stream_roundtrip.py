#!/usr/bin/env python3
import argparse
import hashlib
import random
import subprocess
import tempfile
from pathlib import Path


SAMPLE_SIZE = 4 * 1024 * 1024
CHUNK_SIZE = 256 * 1024
LEVELS = tuple(range(1, 10))


def sha256(path):
    digest = hashlib.sha256()

    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)

    return digest.hexdigest()


def write_random(path):
    rng = random.Random(0x4B414E5A49)
    remaining = SAMPLE_SIZE

    with path.open("wb") as target:
        while remaining > 0:
            size = min(CHUNK_SIZE, remaining)
            target.write(bytes(rng.getrandbits(8) for _ in range(size)))
            remaining -= size


def write_structured(path):
    line = (
        b"kanzi stream transform check 0123456789 "
        b"abcdefghijklmnopqrstuvwxyz repeated fields ROLZ RANK SRT "
        + b"A" * 96
        + b"\n"
    )
    chunk = (line * ((CHUNK_SIZE // len(line)) + 1))[:CHUNK_SIZE]
    remaining = SAMPLE_SIZE

    with path.open("wb") as target:
        while remaining > 0:
            target.write(chunk[: min(CHUNK_SIZE, remaining)])
            remaining -= CHUNK_SIZE


def write_mixed(path):
    rng = random.Random(0x53545245414D)
    chunks = SAMPLE_SIZE // CHUNK_SIZE

    with path.open("wb") as target:
        for block in range(chunks):
            mode = block % 4

            if mode == 0:
                data = bytes((idx + block) & 255 for idx in range(CHUNK_SIZE))
            elif mode == 1:
                data = bytes(65 + (((idx // 257) + block) % 26) for idx in range(CHUNK_SIZE))
            elif mode == 2:
                data = bytes(rng.getrandbits(8) for _ in range(CHUNK_SIZE))
            else:
                data = b"\0" * CHUNK_SIZE

            target.write(data)


def run(command):
    print("+ " + " ".join(str(part) for part in command), flush=True)
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kanzi", type=Path)
    args = parser.parse_args()

    kanzi = args.kanzi.resolve()

    if not kanzi.is_file():
        raise SystemExit(f"Missing Kanzi executable: {kanzi}")

    with tempfile.TemporaryDirectory(prefix="kanzi-stream-") as tmp_dir:
        root = Path(tmp_dir)
        samples = {
            "random": root / "random-4m.bin",
            "structured": root / "structured-4m.txt",
            "mixed": root / "mixed-4m.bin",
        }
        writers = {
            "random": write_random,
            "structured": write_structured,
            "mixed": write_mixed,
        }

        for name, writer in writers.items():
            writer(samples[name])

        for name, source in samples.items():
            source_hash = sha256(source)

            for level in LEVELS:
                compressed = root / f"{name}-l{level}.knz"
                decoded = root / f"{name}-l{level}.out"
                run([str(kanzi), "-c", "-i", str(source), "-o", str(compressed), "-f", "-b", "1m", "-l", str(level), "-x32", "-j", "1", "-v", "1"])
                run([str(kanzi), "-d", "-i", str(compressed), "-o", str(decoded), "-f", "-j", "1", "-v", "1"])
                decoded_hash = sha256(decoded)

                if decoded_hash != source_hash:
                    raise SystemExit(f"SHA-256 mismatch for {name} level {level}")

                print(
                    f"{name} level {level}: {source.stat().st_size} => "
                    f"{compressed.stat().st_size}, SHA-256 match",
                    flush=True,
                )


if __name__ == "__main__":
    main()
