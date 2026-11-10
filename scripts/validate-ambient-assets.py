#!/usr/bin/env python3
"""Validate optional AUDIO5 content using only the Python standard library."""
import argparse
import hashlib
import json
from pathlib import Path
import wave

CATEGORIES = (
    "ventilation", "electrical", "machinery_light", "machinery_heavy", "computer",
    "steam", "hydraulic", "metal_creak", "structural", "distant_impact",
    "distant_mechanical", "organic", "environmental_drone",
)
DECKS = (
    "generic", "reactor", "medical", "research", "maintenance", "storage",
    "flightdeck", "executive", "engineering", "security", "bridge", "groves",
)
BITRATES = (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0)
RATES = (44100, 48000, 32000)


def duration(path: Path) -> float:
    if path.suffix.lower() == ".wav":
        with wave.open(str(path), "rb") as source:
            if source.getnchannels() not in (1, 2) or source.getframerate() <= 0:
                raise ValueError("unsupported WAV channels or sample rate")
            return source.getnframes() / source.getframerate()
    if path.suffix.lower() != ".mp3":
        raise ValueError("unsupported format")
    with path.open("rb") as source:
        header = source.read(10)
        skip = 0
        if header[:3] == b"ID3":
            skip = 10 + ((header[6] & 127) << 21) + ((header[7] & 127) << 14) + ((header[8] & 127) << 7) + (header[9] & 127)
        source.seek(skip)
        probe = source.read(4096)
    for at in range(len(probe) - 4):
        word = int.from_bytes(probe[at:at + 4], "big")
        # MPEG-1, Layer III, legal CBR frame header. Duration is a bounded
        # estimate; final loop-point analysis remains a listening task.
        if word & 0xFFFE0000 != 0xFFFA0000:
            continue
        bitrate = BITRATES[(word >> 12) & 15]
        rate_index = (word >> 10) & 3
        if bitrate and rate_index < 3:
            return max(0.0, (path.stat().st_size - skip) * 8 / (bitrate * 1000))
    raise ValueError("no readable MPEG-1 Layer III frame header")


def validate(root: Path) -> dict:
    entries, issues, hashes = [], [], {}
    if root.is_dir():
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(root)
            parts = rel.parts
            if len(parts) != 3 or parts[0] not in DECKS or parts[1] not in CATEGORIES:
                issues.append(f"{rel}: expected <deck>/<category>/<file>")
                continue
            try:
                seconds = duration(path)
                if seconds < 0.1:
                    raise ValueError("zero-length or implausibly short media")
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                if digest in hashes:
                    issues.append(f"{rel}: duplicate SHA-256 of {hashes[digest]}")
                else:
                    hashes[digest] = str(rel)
                entries.append({"path": str(rel).replace("\\", "/"), "deck": parts[0],
                                "category": parts[1], "kind": "loop" if parts[2].startswith(("loop_", "layer_")) else "one_shot",
                                "duration_seconds_estimate": round(seconds, 2), "sha256": digest})
            except (OSError, EOFError, wave.Error, ValueError) as error:
                issues.append(f"{rel}: {error}")
    empty = [f"{deck}/{category}" for deck in DECKS for category in CATEGORIES
             if not any(item["deck"] == deck and item["category"] == category for item in entries)]
    return {"root": str(root), "asset_count": len(entries), "assets": entries,
            "issues": issues, "empty_categories": empty,
            "zero_assets_supported": len(entries) == 0}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path("audio/ambient"))
    parser.add_argument("--json", type=Path, help="write deterministic QA report/manifest")
    args = parser.parse_args()
    report = validate(args.root)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"AMBIENT_ASSETS={report['asset_count']} ISSUES={len(report['issues'])} EMPTY_CATEGORIES={len(report['empty_categories'])}")
    for issue in report["issues"]:
        print("ISSUE", issue)
    if report["zero_assets_supported"]:
        print("ZERO_ASSETS=SUPPORTED")
    return 1 if report["issues"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
