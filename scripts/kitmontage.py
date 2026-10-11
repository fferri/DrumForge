#!/usr/bin/env python3
"""Join rendered kits into one montage for a single listening pass.

    python3 scripts/kitmontage.py <render-dir> <montage.wav> [--gap 1.2]

Each kit is normalised to the same peak so the montage compares *character* rather than
level, which matters because the lanes deliberately sit at different levels (industrial
hot, ambient quiet) and a 15 dB jump between entries makes listening tiring. Absolute
levels are printed in the index instead.

Prints a timestamped index - the point of the exercise is that a whole batch can be judged
in one sitting and rejected by number.
"""

import argparse
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kitmetrics import read_wav, measure                       # noqa: E402


def pretty_name(filename):
    """in-01scrapyard.wav -> IN-01 Scrapyard"""
    stem = os.path.splitext(filename)[0]
    if "-" in stem:
        lane, rest = stem.split("-", 1)
        digits = "".join(c for c in rest if c.isdigit())
        words = "".join(c for c in rest if not c.isdigit())
        # kit names are lost in the filename (spaces were stripped), so re-split on the
        # original capitalisation is impossible; show the readable stem instead
        return f"{lane.upper()}-{digits} {words.title()}"
    return stem


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    parser.add_argument("output")
    parser.add_argument("--gap", type=float, default=1.2)
    parser.add_argument("--peak", type=float, default=0.89, help="target peak, linear")
    args = parser.parse_args()

    # Aggressive to calm, so the montage builds downward rather than opening with the
    # quietest material.
    lane_order = {"in": 0, "id": 1, "el": 2, "am": 3}

    def sort_key(name):
        prefix = name[:2].lower()
        return (lane_order.get(prefix, 9), name)

    names = sorted((f for f in os.listdir(args.directory)
                    if f.lower().endswith(".wav") and not f.startswith("montage")),
                   key=sort_key)
    if not names:
        print(f"no .wav files in {args.directory}", file=sys.stderr)
        return 1

    rate = 48000
    gap = np.zeros(int(args.gap * rate))
    pieces, index = [], []
    clock = 0.0

    for name in names:
        x, rate = read_wav(os.path.join(args.directory, name))
        m = measure(x, rate)

        if len(x) == 0:
            continue

        x = x / max(np.max(np.abs(x)), 1e-9) * args.peak
        index.append((clock, pretty_name(name), m))
        pieces.append(x)
        pieces.append(gap)
        clock += len(x) / rate + args.gap

    audio = np.concatenate(pieces) if pieces else np.zeros(1)
    stereo = np.repeat(audio[:, None], 2, axis=1)

    data = np.clip(stereo, -1.0, 1.0)
    ints = (data * 8388607.0).astype("<i4")
    raw = ints[:, :, None] >> np.array([0, 8, 16], dtype="<i4")
    raw = raw.astype(np.uint8).tobytes()

    with wave.open(args.output, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(rate)
        w.writeframes(raw)

    print(f"montage: {args.output}  ({clock:.1f}s, {len(index)} kits)\n")
    print(f"{'time':>7}  {'kit':30} {'crest':>7} {'peak':>8}")
    print("-" * 60)
    for stamp, name, m in index:
        minutes, seconds = divmod(stamp, 60)
        print(f"{int(minutes):d}:{seconds:04.1f}  {name[:30]:30} "
              f"{m['crest']:+7.1f} {m['peak_db']:+8.1f}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
