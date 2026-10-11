#!/usr/bin/env python3
"""Measure rendered DrumForge kits against the reference targets.

The kit design harness has two halves. This is the measuring half: KitLab renders a
.dfkit through a pattern matched to its lane, and this compares the result with what the
reference recordings for that lane actually measure.

    python3 scripts/kitmetrics.py <render-dir> [--json <out.json>]

Per-lane targets come from nine industrial/IDM/electronica recordings plus four ambient
ones, all 48 kHz. The band figures are dB relative to total energy; tilt is the slope of
the log spectrum in dB per decade, so more negative means darker.

The number that matters most is crest factor. It orders the four lanes cleanly and
monotonically (industrial ~10 dB, electronica ~12, IDM ~17, ambient ~20) while spectral
tilt does not separate them at all - which is why the lanes get different patterns rather
than just different sounds.
"""

import argparse
import json
import os
import struct
import sys
import wave

import numpy as np

SAMPLE_RATE = 48000

BANDS = [
    ("sub", 30, 60),
    ("bass", 60, 120),
    ("loMid", 120, 400),
    ("mid", 400, 1500),
    ("hiMid", 1500, 5000),
    ("high", 5000, 16000),
]

# lane -> (crest target, acceptable range, tilt target, band targets)
LANE_TARGETS = {
    "IN": dict(name="industrial", crest=9.9, crest_range=(6.0, 13.5), tilt=-24.5,
               bands=[-9.3, -9.2, -9.2, -8.1, -11.1, -20.0]),
    "ID": dict(name="idm", crest=16.5, crest_range=(13.5, 19.0), tilt=-18.3,
               bands=[-8.3, -5.9, -6.0, -11.6, -10.1, -13.9]),
    "EL": dict(name="electronica", crest=12.3, crest_range=(9.5, 15.5), tilt=-19.3,
               bands=[-11.4, -5.1, -5.4, -9.9, -10.7, -17.4]),
    "AM": dict(name="ambient", crest=19.5, crest_range=(16.5, 24.0), tilt=-19.0,
               bands=[-8.3, -7.7, -7.4, -6.3, -13.6, -19.0]),
    # No techno recordings in the reference set, so this is a prior sitting between the
    # industrial and electronica measurements rather than something measured. It exists to
    # catch gross problems, not to judge the kits.
    "TE": dict(name="techno", crest=11.5, crest_range=(7.5, 15.5), tilt=-22.0,
               bands=[-10.0, -7.0, -7.0, -9.0, -11.0, -18.5]),
}


def read_wav(path):
    """Mono float array. Handles the 16/24/32-bit files these tools write."""
    with wave.open(path, "rb") as w:
        channels, width, rate, frames = (w.getnchannels(), w.getsampwidth(),
                                         w.getframerate(), w.getnframes())
        raw = w.readframes(frames)

    if width == 2:
        a = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8)
        b = b[: len(b) // 3 * 3].reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v)
        a = v.astype(np.float64) / 8388608.0
    elif width == 4:
        a = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{path}: unsupported sample width {width}")

    a = a[: len(a) // channels * channels].reshape(-1, channels)
    return a.mean(axis=1), rate


def measure(x, rate=SAMPLE_RATE):
    x = x - x.mean()
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    rms = float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0

    n = 1 << int(np.log2(min(len(x), 1 << 22)))
    X = np.abs(np.fft.rfft(x[:n] * np.hanning(n))) ** 2
    fr = np.fft.rfftfreq(n, 1 / rate)
    total = X.sum() or 1e-20

    mask = (fr > 40) & (fr < 16000) & (X > 0)
    tilt = float(np.polyfit(np.log10(fr[mask]), 10 * np.log10(X[mask]), 1)[0])

    bands = [10 * np.log10(max(X[(fr >= lo) & (fr < hi)].sum(), 1e-20) / total)
             for _, lo, hi in BANDS]

    return dict(
        peak_db=20 * np.log10(peak) if peak > 0 else -200.0,
        rms_db=20 * np.log10(rms) if rms > 0 else -200.0,
        crest=20 * np.log10(peak / rms) if rms > 0 else 0.0,
        tilt=tilt,
        bands=bands,
    )


def lane_of(name):
    prefix = name.split("-")[0].strip().upper()
    return prefix if prefix in LANE_TARGETS else None


def verdict(metrics, target):
    """Gate on spectral character and level sanity; report crest as advisory.

    Crest was originally the gate, on the strength of it separating the four lanes in the
    reference recordings. Measuring the actual lever response showed why that was wrong:
    drive moves crest by 0.1 dB (the distortion is level-compensated), and track level
    moves it only ~2.5 dB for +9 dB because it works through the master limiter. What
    actually moves crest is how much the hits overlap, so a short dry kit and a long washy
    kit in the *same* lane differ more than two different lanes do.

    Crest in a reference track is therefore an emergent property of arrangement and
    mastering, not something a single kit controls - and chasing it would mean baking a
    mastering limiter into an instrument, which is the wrong place for it. It stays in the
    report as information, with a lane advisory, and does not fail a kit.

    The bands are advisory for the same reason: an all-metal kit with no kick has no sub
    content, and that is the design, not a defect.
    """
    problems = []

    if abs(metrics["tilt"] - target["tilt"]) > 12.0:
        problems.append(f"tilt {metrics['tilt']:+.1f} far from {target['tilt']:+.1f}")

    if metrics["peak_db"] > -0.3:
        problems.append(f"peak {metrics['peak_db']:+.1f} dB is slamming the master limiter")

    if metrics["peak_db"] < -16.0:
        problems.append(f"peak {metrics['peak_db']:+.1f} dB is very quiet for this lane")

    return problems


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", help="directory of rendered .wav files")
    parser.add_argument("--json", help="also write the raw measurements here")
    args = parser.parse_args()

    files = sorted(f for f in os.listdir(args.directory) if f.lower().endswith(".wav"))
    if not files:
        print(f"no .wav files in {args.directory}", file=sys.stderr)
        return 1

    results = {}
    failures = 0

    print(f"{'kit':34} {'lane':12} {'crest':>7} {'want':>6} {'tilt':>7} {'want':>7} {'peak':>7}  "
          + " ".join(f"{n:>6}" for n, _, _ in BANDS))
    print("-" * 128)

    for name in files:
        path = os.path.join(args.directory, name)
        try:
            x, rate = read_wav(path)
        except Exception as error:                       # noqa: BLE001
            print(f"{name:34} unreadable: {error}")
            failures += 1
            continue

        m = measure(x, rate)
        results[os.path.splitext(name)[0]] = m

        lane = lane_of(name)
        if lane is None:
            print(f"{name:34} {'-':12} {m['crest']:+7.1f} {'':>6} {m['tilt']:+7.1f} {'':>7} "
                  f"{m['peak_db']:+7.1f}  " + " ".join(f"{v:6.1f}" for v in m["bands"]))
            continue

        target = LANE_TARGETS[lane]
        problems = verdict(m, target)
        if problems:
            failures += 1

        lo, hi = target["crest_range"]
        mark = "!" if problems else ("." if not lo <= m["crest"] <= hi else " ")

        print(f"{name[:34]:34} {target['name']:12} {m['crest']:+7.1f} {target['crest']:+6.1f} "
              f"{m['tilt']:+7.1f} {target['tilt']:+7.1f} {m['peak_db']:+7.1f}  "
              + " ".join(f"{v:6.1f}" for v in m["bands"]) + mark)

        for problem in problems:
            print(f"{'':36}  -> {problem}")

    print("-" * 128)
    print(f"{len(files)} kit(s), {failures} with problems")
    print("  ! = fails the gate   . = crest outside the lane advisory (informational)")

    if args.json:
        with open(args.json, "w") as handle:
            json.dump(results, handle, indent=2)
        print(f"wrote {args.json}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
