#!/usr/bin/env python3
"""Replay riptide timelines (riptide --trace FILE) in the terminal.

Usage: bench/pipeline_term.py [--seconds S] [--auto] TRACE.json[=LABEL] [TRACE.json[=LABEL] ...]

The terminal twin of bench/pipeline_viz.py, with the same data: the six stages with
their measured times, one row per stage showing what every chunk is doing, and every
thread's tasks, replayed in slow motion. The whole of the longest run plays in
--seconds (default 20); every run uses the same slow-motion factor.

Keys: space pause or play, left and right step, 1-9 pick a run, z zoom the threads
to the current stage, + and - speed, r restart, q quit. With --auto (or when not
run in a terminal) every run plays once and the program exits.
"""

import os
import select
import shutil
import sys
import termios
import textwrap
import time
import tty

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pipeline_viz import CATEGORIES, load  # noqa: E402

COLOURS = {
    "decode": (59, 117, 196), "chain": (150, 150, 150), "window": (232, 142, 38), "gpu": (204, 62, 62),
    "replace": (70, 160, 90), "verify": (140, 90, 180), "wait": (110, 116, 126),
}
NOW = (242, 190, 30)
DIM = (70, 76, 86)
STAGES = {
    "partition": ("Find chunks", "decode",
                  "The compressed file is cut into chunks of the same size, one task per chunk. Nothing is decoded yet."),
    "decode": ("Decode", "decode",
               "Every thread decodes its own chunk at the same time. A chunk does not know the 32 KiB before it, "
               "so a back-reference into that area is written as a marker (0x8000 + j means byte j of the window "
               "before this chunk) instead of a byte."),
    "chain": ("Link chunks", "chain",
              "Each chunk must end exactly where the next one starts. Chunks that started at a wrong guess are "
              "thrown away and decoded again."),
    "windows": ("Window (M3)", "window", ""),
    "replace": ("Replace markers", "replace",
                "Every chunk now knows the 32 KiB before it, so all threads swap their markers for the real bytes "
                "at the same time."),
    "verify": ("CRC-32 check", "verify",
               "Each thread computes the CRC-32 of its piece of the output. The pieces are combined into one CRC "
               "and checked against the gzip trailer."),
}
STEPS = {
    "window maps": "First every thread turns the last 32 KiB of each of its chunks into a window map: for each of "
                   "the 32,768 positions, the byte or the marker that ends up there. Fully parallel, and not "
                   "counted as the window step.",
    "prepare": "The maps are ready; the scan is about to start (riptide counts the window step from here).",
    "serial chain": "One by one, as rapidgzip does it: each chunk's window is worked out from the chunk before it, "
                    "so every chunk waits for the previous one. Row 2 fills from left to right.",
    "compose block": "Scan step 1 of 3: the chunks are split into {b} blocks, one per thread. Every block composes "
                     "the maps of its own chunks into one map, all blocks at the same time.",
    "carry between blocks": "Scan step 2 of 3: one short serial pass carries the real window across the {b} block "
                            "borders. Only {b} steps have to wait for each other.",
    "resolve block": "Scan step 3 of 3: every block knows the window it starts with, so all {b} blocks work out "
                     "the window of each of their chunks at the same time.",
    "compose blocks": "Scan step 1 of 3, on the GPU: {b} blocks compose their maps at the same time. Each step is "
                      "32,768 lookups that do not depend on each other, which suits the GPU.",
    "resolve blocks": "Scan step 3 of 3, on the GPU: all {b} blocks work out the window of each of their chunks "
                      "at the same time.",
}


def fg(rgb, text):
    return f"\x1b[38;2;{rgb[0]};{rgb[1]};{rgb[2]}m{text}\x1b[0m"


def mix(rgb, amount):
    """Fades a colour towards the dark background; amount 1 keeps it."""
    return tuple(int(DIM[i] * 0.4 + (c - DIM[i] * 0.4) * amount) for i, c in enumerate(rgb))


def fmt(ms):
    return f"{ms:.0f}" if ms >= 100 else f"{ms:.1f}" if ms >= 10 else f"{ms:.2f}"


def bold(text):
    return f"\x1b[1m{text}\x1b[0m"


class Player:
    def __init__(self, runs, seconds, auto):
        self.runs, self.seconds, self.auto = runs, seconds, auto
        self.longest = max(r["total"] for r in runs)
        self.index, self.t, self.playing, self.zoom = 0, 0.0, True, False
        self.cache = {}

    @property
    def run(self):
        return self.runs[self.index]

    def stage(self):
        k = 0
        for i, (_, s, _e) in enumerate(self.run["stages"]):
            if self.t >= s:
                k = i
        return k

    def window_text(self):
        r, key = self.run, None
        for name, s, _e in r["window_steps"]:
            if self.t >= s:
                key = name
        if key == "window maps" and self.t >= r["window_steps"][0][2]:
            key = "prepare"
        return STEPS.get(key, "The window maps are combined with a parallel prefix scan.").replace("{b}", str(r["blocks"]))

    def chunk_state(self, row, i):
        c, t = self.run["chunk"], self.t
        if row == 1 and c["cs"][i] >= 0:
            if t < c["cs"][i]:
                return 0
            if t < c["ce"][i]:
                return 1
            if t < c["ks"][i]:
                return 3
        s, e = {0: ("ds", "de"), 1: ("ks", "ke"), 2: ("rs", "re")}[row]
        s, e = c[s][i], c[e][i]
        return 2 if s < 0 else 0 if t < s else 1 if t < e else 2

    def strip_row(self, row, width, colour):
        n, out = self.run["chunks"], []
        for col in range(width):
            i0 = col * n // width
            i1 = max(i0 + 1, (col + 1) * n // width)
            states = [self.chunk_state(row, i) for i in range(i0, min(i1, n))]
            done = states.count(2) / len(states)
            if 1 in states:
                out.append(fg(NOW, "█"))
            elif done == 1:
                out.append(fg(colour, "█"))
            elif done > 0:
                out.append(fg(colour, "▓" if done > 0.66 else "▒" if done > 0.33 else "░"))
            elif 3 in states:
                out.append(fg(mix(colour, 0.45), "▒"))
            else:
                out.append(fg(DIM, "·"))
        return "".join(out)

    def view(self):
        if self.zoom:
            _, s, e = self.run["stages"][self.stage()]
            pad = max((e - s) * 0.04, 0.01)
            return max(0.0, s - pad), e + pad
        return 0.0, self.run["total"]

    def lane_grid(self, width):
        """For each thread and column, the category that fills most of the column."""
        v0, v1 = self.view()
        key = (self.index, width, round(v0, 4), round(v1, 4))
        if key not in self.cache:
            rows = len(self.run["threads"])
            cover = [[None] * width for _ in range(rows)]
            amount = [[0.0] * width for _ in range(rows)]
            step = (v1 - v0) / width
            for row, cat, _name, s, d, _args in self.run["work"]:
                e = s + d
                if e <= v0 or s >= v1:
                    continue
                for col in range(max(0, int((s - v0) / step)), min(width, int((e - v0) / step) + 1)):
                    a, b = v0 + col * step, v0 + (col + 1) * step
                    overlap = min(b, e) - max(a, s)
                    if overlap > amount[row][col]:
                        amount[row][col], cover[row][col] = overlap, CATEGORIES[cat]
            self.cache[key] = cover
        return self.cache[key]

    def frame(self, cols, lines):
        r, t = self.run, self.t
        out = []
        tabs = "  ".join(bold(fg(NOW, f"[{i + 1}] {x['label']}")) if i == self.index else f"[{i + 1}] {x['label']}"
                         for i, x in enumerate(self.runs))
        out.append(bold("riptide pipeline, replayed from the trace") + f"   {fmt(t)} ms of {fmt(r['total'])} ms"
                   + ("" if self.playing else "   " + bold("PAUSED")))
        out.append(tabs)
        out.append(f"{r['chunks']:,} chunks, {r['output_bytes'] / 1e6:.0f} MB out, "
                   f"{r['output_bytes'] / 1e6 / (r['total'] / 1000):,.0f} MB/s, whole run plays in "
                   f"{self.seconds:g} s   keys: space 1-{len(self.runs)} arrows z + - r q")
        out.append("")

        k = self.stage()
        cell = max(14, (cols - 2) // 6)
        names, times, bars, scans = [], [], [], []
        for i, (name, s, e) in enumerate(r["stages"]):
            title, cat = STAGES[name][:2]
            colour = COLOURS["gpu" if name == "windows" and r["strategy"].startswith("gpu") else cat]
            label = f"{i + 1} {title}"[:cell - 1].ljust(cell)
            names.append(bold(fg(colour, label)) if i == k and t < r["total"] else label)
            times.append(f"{fmt(e - s)} ms, {100 * (e - s) / r['total']:.1f}%"[:cell - 1].ljust(cell))
            scans.append((f"scan {fmt(r['scan'][1] - r['scan'][0])} ms" if name == "windows" else "")
                         [:cell - 1].ljust(cell))
            filled = int((cell - 2) * max(0.0, min(1.0, (t - s) / max(e - s, 1e-9))))
            bars.append(fg(colour, "━" * filled) + fg(DIM, "━" * (cell - 2 - filled)) + "  ")
        out += ["".join(names), "".join(times), "".join(scans), "".join(bars), ""]

        name, s, e = r["stages"][k]
        if t >= r["total"]:
            head, body = "Done: the output is complete and checked.", \
                f"{r['output_bytes'] / 1e6:.0f} MB in {fmt(r['total'])} ms. Press r to replay or 1-{len(self.runs)} for another run."
        else:
            title = "Window step (M3)" if name == "windows" else STAGES[name][0]
            head = f"Now: stage {k + 1} of 6, {title} ({fmt(e - s)} ms)"
            body = self.window_text() if name == "windows" else STAGES[name][2]
        out.append(bold(head))
        out += textwrap.wrap(body, cols - 2)[:3]
        c, n = r["chunk"], r["chunks"]
        count = lambda key: sum(1 for v in c[key] if 0 <= v <= t)
        busy = sum(1 for w in r["work"] if w[3] <= t < w[3] + w[4] and CATEGORIES[w[1]] != "wait")
        out.append(f"decoded {count('de'):,}/{n:,}   windows known {count('ke'):,}/{n:,}   "
                   f"real bytes {count('re'):,}/{n:,}   threads busy {busy}")
        out.append("")

        label_w, gpu = 22, r["strategy"].startswith("gpu")
        width = max(10, cols - label_w - 1)
        out.append(bold("Every chunk") + f"  ({max(1, -(-n // width))} chunk(s) per character, "
                   + fg(NOW, "█") + " = being worked on now)")
        for row, (label, cat) in enumerate((("1 decoded, markers in", "decode"),
                                           ("2 window known", "gpu" if gpu else "window"),
                                           ("3 real bytes written", "replace"))):
            out.append(label.ljust(label_w) + self.strip_row(row, width, COLOURS[cat]))
        crc_all = sum(e - s for s, e in r["crc"])
        crc = sum(max(0.0, min(t, e) - s) for s, e in r["crc"]) / crc_all if crc_all else 0
        out.append("4 CRC-32 checked".ljust(label_w) + fg(COLOURS["verify"], "█" * int(width * crc))
                   + fg(DIM, "·" * (width - int(width * crc))))
        out.append("")

        v0, v1 = self.view()
        lane_w = max(10, cols - 11)
        grid = self.lane_grid(lane_w)
        step = (v1 - v0) / lane_w
        out.append(bold("Every thread") + ("  (zoomed to the current stage, z for the whole run)" if self.zoom
                                           else "  (z zooms to the current stage)"))
        room = max(3, lines - len(out) - 3)
        threads = r["threads"]
        for row, thread in enumerate(threads[:room]):
            chars = []
            for col in range(lane_w):
                cat, start = grid[row][col], v0 + col * step
                if cat is None:
                    chars.append(fg(DIM, "·") if start < t else " ")
                elif start < t:
                    chars.append(fg(COLOURS[cat], "█"))
                else:
                    chars.append(fg(mix(COLOURS[cat], 0.35), "░"))
            out.append(thread[:9].ljust(10) + "".join(chars))
        axis = [" "] * lane_w
        if v0 <= t <= v1:
            axis[min(lane_w - 1, int((t - v0) / step))] = fg(NOW, "▲")
        out.append(" " * 10 + "".join(axis))
        out.append(" " * 10 + f"{fmt(v0)} ms".ljust(lane_w - 10) + f"{fmt(v1)} ms".rjust(10))
        legend = [fg(COLOURS[c], "█") + " " + name for c, name in
                  (("decode", "decode"), ("window", "window maps / CPU scan"), ("gpu", "GPU scan"),
                   ("replace", "replace"), ("verify", "CRC-32"), ("wait", "waiting for GPU"))
                  if any(CATEGORIES[w[1]] == c for w in r["work"])]
        out.append("   ".join(legend))
        return out[:lines]

    def key(self, k):
        r = self.run
        if k == " ":
            self.playing = not self.playing
            if self.playing and self.t >= r["total"]:
                self.t = 0.0
        elif k == "\x1b[C":
            self.t = min(r["total"], self.t + r["total"] / 100)
        elif k == "\x1b[D":
            self.t = max(0.0, self.t - r["total"] / 100)
        elif k in "123456789" and int(k) <= len(self.runs):
            self.index, self.t, self.playing = int(k) - 1, 0.0, True
        elif k == "z":
            self.zoom = not self.zoom
        elif k == "+":
            self.seconds = max(2.5, self.seconds / 2)
        elif k == "-":
            self.seconds = min(1200, self.seconds * 2)
        elif k == "r":
            self.t, self.playing = 0.0, True
        elif k == "q":
            return False
        return True

    def advance(self, dt):
        if not self.playing:
            return True
        self.t += dt * self.longest / self.seconds
        if self.t >= self.run["total"]:
            self.t = self.run["total"]
            if self.auto:
                if self.index + 1 == len(self.runs):
                    return False
                self.index, self.t = self.index + 1, 0.0
            else:
                self.playing = False
        return True


def read_key(timeout):
    ready, _, _ = select.select([sys.stdin], [], [], timeout)
    if not ready:
        return None
    k = os.read(sys.stdin.fileno(), 1).decode(errors="ignore")
    if k == "\x1b":
        more, _, _ = select.select([sys.stdin], [], [], 0.01)
        if more:
            k += os.read(sys.stdin.fileno(), 2).decode(errors="ignore")
    return k


def main():
    args, seconds, auto = sys.argv[1:], 20.0, False
    if "--auto" in args:
        args.remove("--auto")
        auto = True
    if "--seconds" in args:
        i = args.index("--seconds")
        seconds = float(args[i + 1])
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    runs = []
    for arg in args:
        path, _, label = arg.partition("=")
        runs.append(load(path, label or os.path.basename(path)))
    interactive = sys.stdin.isatty() and not auto
    player = Player(runs, seconds, auto or not interactive)

    saved = termios.tcgetattr(sys.stdin) if interactive else None
    sys.stdout.write("\x1b[?1049h\x1b[?25l")
    try:
        if interactive:
            tty.setcbreak(sys.stdin)
        last = time.monotonic()
        while True:
            cols, lines = shutil.get_terminal_size((120, 40))
            sys.stdout.write("\x1b[H" + "\n".join(line + "\x1b[K" for line in player.frame(cols, lines)) + "\x1b[J")
            sys.stdout.flush()
            if interactive:
                k = read_key(1 / 30)
                if k is not None and not player.key(k):
                    break
            else:
                time.sleep(1 / 30)
            now = time.monotonic()
            if not player.advance(now - last):
                time.sleep(1.5)
                break
            last = now
    except KeyboardInterrupt:
        pass
    finally:
        if saved is not None:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, saved)
        sys.stdout.write("\x1b[?25h\x1b[?1049l")
        sys.stdout.flush()
    for r in runs:
        print(f"   {r['label']}: {fmt(r['total'])} ms, window stage "
              f"{fmt(next(e - s for n, s, e in r['stages'] if n == 'windows'))} ms "
              f"(scan {fmt(r['scan'][1] - r['scan'][0])} ms), {r['chunks']:,} chunks")


if __name__ == "__main__":
    main()
