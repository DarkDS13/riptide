#!/usr/bin/env python3
"""Draw a riptide timeline (riptide --trace FILE) as a PNG image.

Usage: bench/plot_trace.py TRACE.json OUTPUT.png [--title TEXT] [--from MS] [--to MS] [--no-legend]

One row per thread (main, worker 0..n-1) plus one row per extra track (the GPU).
Above them, a band shows the pipeline stages. Bars are coloured by what the task
did; small ticks at the top of a row mark work steals. --from and --to zoom into a
time range, in milliseconds from the start of the trace.

The same file opens interactively in ui.perfetto.dev or chrome://tracing. This
script exists for figures in the report and the slides, and needs only Pillow.
"""

import argparse
import json

from PIL import Image, ImageDraw, ImageFont

COLOURS = {
    "decode": (59, 117, 196),
    "chain": (150, 150, 150),
    "window": (232, 142, 38),
    "gpu": (204, 62, 62),
    "replace": (70, 160, 90),
    "verify": (140, 90, 180),
    "wait": (205, 205, 205),
}
STAGE_COLOURS = {
    "partition": (210, 210, 210),
    "decode": (186, 207, 236),
    "chain": (220, 220, 220),
    "windows": (247, 210, 165),
    "replace": (190, 225, 196),
    "verify": (215, 196, 232),
}
LEGEND = [("decode", "decode chunk"), ("window", "window resolution (CPU)"),
          ("gpu", "window resolution (GPU)"), ("replace", "marker replacement"),
          ("verify", "CRC-32"), ("chain", "chain / fallback"), ("wait", "waiting for the GPU")]


def font(size):
    for path in ("/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/SFNS.ttf",
                 "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def load(path):
    with open(path) as f:
        events = json.load(f)["traceEvents"]
    names, order = {}, {}
    for e in events:
        if e.get("ph") == "M" and e["name"] == "thread_name":
            names[e["tid"]] = e["args"]["name"]
        elif e.get("ph") == "M" and e["name"] == "thread_sort_index":
            order[e["tid"]] = e["args"]["sort_index"]
    spans = [e for e in events if e.get("ph") == "X"]
    instants = [e for e in events if e.get("ph") == "i"]
    return names, order, spans, instants


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("output")
    parser.add_argument("--title", default="")
    parser.add_argument("--from", dest="start", type=float, default=None)
    parser.add_argument("--to", dest="end", type=float, default=None)
    parser.add_argument("--width", type=int, default=2400)
    parser.add_argument("--no-legend", action="store_true", help="omit the legend, e.g. for all but one stacked panel")
    args = parser.parse_args()

    names, order, spans, instants = load(args.trace)
    stages = [e for e in spans if e["cat"] == "stage"]
    work = [e for e in spans if e["cat"] != "stage"]
    t0 = args.start if args.start is not None else 0.0
    t1 = args.end if args.end is not None else max(e["ts"] + e["dur"] for e in spans) / 1000
    tids = sorted({e["tid"] for e in spans} | {e["tid"] for e in instants},
                  key=lambda t: (order.get(t, 1000), names.get(t, "")))

    scale = 2
    label_w, right, top, row_h, gap = 150 * scale, 30 * scale, 70 * scale, 26 * scale, 6 * scale
    stage_h, axis_h, legend_h = 26 * scale, 40 * scale, 64 * scale
    if args.no_legend:
        legend_h = 0
    width = args.width
    height = top + stage_h + gap + len(tids) * (row_h + gap) + axis_h + legend_h
    image = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(image)
    small, normal, big = font(12 * scale), font(14 * scale), font(18 * scale)
    plot_w = width - label_w - right

    def x(ms):
        return label_w + (ms - t0) / (t1 - t0) * plot_w

    def bar(y, h, begin_ms, end_ms, colour, outline=None):
        a, b = max(x(begin_ms), label_w), min(x(end_ms), label_w + plot_w)
        if b < label_w or a > label_w + plot_w:
            return None
        b = max(b, a + 1)
        draw.rectangle([a, y, b, y + h], fill=colour, outline=outline)
        return a, b

    if args.title:
        draw.text((label_w, 20 * scale), args.title, fill="black", font=big)

    y = top
    draw.text((10 * scale, y + 5 * scale), "stage", fill="black", font=normal)
    for e in stages:
        begin, end = e["ts"] / 1000, (e["ts"] + e["dur"]) / 1000
        span = bar(y, stage_h, begin, end, STAGE_COLOURS.get(e["name"], (230, 230, 230)), (120, 120, 120))
        if span and span[1] - span[0] > 60 * scale:
            draw.text((span[0] + 4 * scale, y + 5 * scale), e["name"], fill="black", font=small)
    y += stage_h + gap

    for tid in tids:
        draw.text((10 * scale, y + 5 * scale), names.get(tid, str(tid)), fill="black", font=normal)
        draw.rectangle([label_w, y, label_w + plot_w, y + row_h], fill=(246, 246, 246))
        # Consecutive tasks alternate between two shades so that back-to-back
        # tasks stay distinguishable.
        for index, e in enumerate(sorted((e for e in work if e["tid"] == tid), key=lambda e: e["ts"])):
            colour = COLOURS.get(e["cat"], (100, 100, 100))
            if index % 2:
                colour = tuple(min(255, int(c * 0.8 + 255 * 0.2)) for c in colour)
            bar(y, row_h, e["ts"] / 1000, (e["ts"] + e["dur"]) / 1000, colour)
        for e in instants:
            if e["tid"] == tid and t0 <= e["ts"] / 1000 <= t1:
                tick = x(e["ts"] / 1000)
                draw.line([tick, y, tick, y + 6 * scale], fill="black", width=scale)
        y += row_h + gap

    # Time axis with about ten ticks at round values.
    span_ms = t1 - t0
    step = next(s for s in (0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000)
                if span_ms / s <= 12)
    draw.line([label_w, y, label_w + plot_w, y], fill="black", width=scale)
    tick = (int(t0 / step) + 1) * step if t0 % step else t0
    while tick <= t1 + 1e-9:
        draw.line([x(tick), y, x(tick), y + 6 * scale], fill="black", width=scale)
        text = f"{tick:g} ms"
        w = draw.textlength(text, font=small)
        draw.text((x(tick) - w / 2, y + 8 * scale), text, fill="black", font=small)
        tick += step
    y += axis_h

    lx = label_w
    entries = [] if args.no_legend else [(c, l) for c, l in LEGEND if any(e["cat"] == c for e in work)]
    if instants and not args.no_legend:
        entries.append((None, "work steal"))
    for category, label in entries:
        entry_w = 24 * scale + draw.textlength(label, font=small) + 30 * scale
        if lx + entry_w > width - right:  # wrap onto a second legend line
            lx, y = label_w, y + 24 * scale
        if category is None:
            draw.line([lx, y, lx, y + 14 * scale], fill="black", width=scale)
            draw.text((lx + 8 * scale, y), label, fill="black", font=small)
            continue
        draw.rectangle([lx, y, lx + 18 * scale, y + 14 * scale], fill=COLOURS[category])
        draw.text((lx + 24 * scale, y), label, fill="black", font=small)
        lx += entry_w

    image.save(args.output)


if __name__ == "__main__":
    main()
