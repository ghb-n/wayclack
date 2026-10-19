#!/usr/bin/env python3
"""Render the WayClack TUI to a demo GIF (no third-party Python packages).

Pipeline:
  1. Run the real TUI inside a pty and capture its ANSI output at scripted
     moments (volume up, mute, switch pack, ...).
  2. Convert each captured screen into a coloured SVG (per-span foreground
     colours, monospaced grid) and rasterise it with ImageMagick.
  3. Stitch the PNG frames into an optimised, looping GIF.

Requires: python3 + ImageMagick (`convert`). Optionally `rsvg-convert`
(used automatically if present for sharper SVG rasterisation).

Usage:  python3 docs/demo/render_gif.py
"""
import os, sys, re, time, pty, select, signal, argparse, shutil, subprocess
import fcntl, struct, termios

# --------------------------------------------------------------------------
# Terminal look
# --------------------------------------------------------------------------
COLS, ROWS = 80, 24
CELL_W, CELL_H = 9, 17          # px per character cell
PAD_X, PAD_Y = 10, 10           # outer padding
PAD_TOP = 26                    # extra room for the traffic-light title bar

BG = "#0d1117"                  # GitHub dark background
BAR = "#161b22"                 # title bar
FG = "#c9d1d9"                  # default text

# 16-colour terminal palette (dark-theme tuned, 256-colour-ish).
PALETTE = {
    30: "#484f58", 31: "#ff7b72", 32: "#4ade80", 33: "#e3b341",
    34: "#79c0ff", 35: "#d2a8ff", 36: "#39d3d0", 37: "#c9d1d9",
    90: "#6e7681",  # bright black / dim
}
DEFAULT_FG = FG

ANSI_RE = re.compile(r"\033\[[0-9;?]*[a-zA-Z]")


class Span:
    __slots__ = ("col", "text", "fg", "bold", "dim")
    def __init__(self, col, text, fg, bold, dim):
        self.col, self.text, self.fg, self.bold, self.dim = col, text, fg, bold, dim


def parse_line(line):
    spans = []
    fg, bold, dim = DEFAULT_FG, False, False
    buf, col = "", 0
    spans_start = 0
    i = 0

    def flush(start):
        nonlocal buf
        if buf:
            spans.append(Span(start, buf, fg, bold, dim))
            buf = ""

    start = 0
    while i < len(line):
        if line[i] == "\033":
            flush(start)
            j = line.find("m", i)
            if j == -1:
                break
            for p in line[i + 2:j].split(";"):
                if p in ("", "0"):
                    fg, bold, dim = DEFAULT_FG, False, False
                elif p == "1":
                    bold = True
                elif p == "2":
                    dim = True
                elif p.isdigit() and int(p) in PALETTE:
                    fg = PALETTE[int(p)]
            i = j + 1
            start = col
            continue
        buf += line[i]
        col += 1
        i += 1
    flush(start)
    return spans


def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def frame_svg(lines):
    W = COLS * CELL_W + PAD_X * 2
    H = ROWS * CELL_H + PAD_Y * 2 + PAD_TOP
    out = []
    out.append(
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d">' % (W, H, W, H))
    out.append('<rect width="%d" height="%d" rx="10" fill="%s"/>' % (W, H, BG))
    # Title bar with traffic lights.
    out.append('<rect width="%d" height="%d" rx="10" fill="%s"/>' % (W, PAD_TOP, BAR))
    out.append('<rect y="%d" width="%d" height="10" fill="%s"/>' % (PAD_TOP - 10, W, BAR))
    for k, c in enumerate(["#ff5f56", "#ffbd2e", "#27c93f"]):
        out.append('<circle cx="%d" cy="%d" r="6" fill="%s"/>'
                   % (20 + k * 20, PAD_TOP // 2, c))
    out.append('<text x="%d" y="%d" font-family="DejaVu Sans Mono, monospace" '
               'font-size="11" fill="#8b949e">wayclack — TUI demo</text>'
               % (W - 190, PAD_TOP // 2 + 4))

    size = 13
    for r, line in enumerate(lines[:ROWS]):
        y = PAD_TOP + PAD_Y + r * CELL_H + 12
        for sp in parse_line(line):
            x = PAD_X + sp.col * CELL_W
            weight = ' font-weight="bold"' if sp.bold else ""
            opacity = ' opacity="%s"' % ("0.55" if sp.dim else "1")
            out.append(
                '<text x="%d" y="%d" font-family="DejaVu Sans Mono, monospace" '
                'font-size="%d" fill="%s"%s%s xml:space="preserve">%s</text>'
                % (x, y, size, sp.fg, weight, opacity, esc(sp.text)))
    out.append("</svg>")
    return "\n".join(out)


def rasterise(svg_path, png_path):
    if shutil.which("rsvg-convert"):
        subprocess.run(["rsvg-convert", "-o", png_path, svg_path], check=True)
    else:
        subprocess.run(["convert", "-background", "none", svg_path, png_path],
                       check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="docs/demo.gif")
    ap.add_argument("--binary", default="./build/wayclack")
    ap.add_argument("--packs", default="docs/demo/packs")
    ap.add_argument("--tmp", default="/tmp/wayclack_frames")
    args = ap.parse_args()

    if not shutil.which("convert"):
        sys.exit("ImageMagick 'convert' is required")

    shutil.rmtree(args.tmp, ignore_errors=True)
    os.makedirs(args.tmp, exist_ok=True)

    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(args.binary,
                 [args.binary, "-p", args.packs, "-d", "/dev/null"])
        os._exit(1)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

    def drain(t=0.4):
        out = b""
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.04)
            if r:
                try:
                    c = os.read(fd, 1 << 20)
                except OSError:
                    break
                if not c:
                    break
                out += c
        return out.decode("utf-8", "replace")

    time.sleep(0.9)
    drain()

    frames = []

    def capture(keys=None, wait=0.5):
        if keys:
            os.write(fd, keys)
        time.sleep(wait)
        text = ANSI_RE.sub("", drain())
        # Rebuild the visible screen: clear-screen frames end with the last
        # full redraw, so take the text after the final clear if present.
        parts = re.split(r"\033\[2J", text)
        screen = parts[-1] if len(parts) > 1 or True else text
        screen = screen.replace("\r", "")
        rows_ = screen.split("\n")
        while rows_ and rows_[-1].strip() == "":
            rows_.pop()
        rows_ = rows_[-ROWS:]
        return rows_

    # Scripted interaction.
    def add(keys=None, wait=0.5):
        rows_ = capture(keys, wait)
        if len(rows_) >= 5:
            frames.append(rows_)

    add(None, 0.8)          # initial screen
    add(b"+", 0.5)          # volume up
    add(b"+", 0.5)
    add(b"+", 0.5)
    add(b"m", 0.55)         # mute
    add(b"m", 0.55)         # unmute
    add(b"n", 0.55)         # next pack
    add(b"p", 0.55)         # previous pack
    add(b"q", 0.4)          # quit

    # De-duplicate consecutive identical frames but keep a final hold.
    pngs = []
    last = None
    for i, rows_ in enumerate(frames):
        key = "\n".join(rows_)
        if key == last:
            continue
        last = key
        svg = os.path.join(args.tmp, "f%02d.svg" % i)
        png = os.path.join(args.tmp, "f%02d.png" % i)
        with open(svg, "w") as f:
            f.write(frame_svg(rows_))
        rasterise(svg, png)
        pngs.append(png)

    if not pngs:
        sys.exit("no frames captured")

    # Build the GIF: a touch longer on the first frame, then steady.
    cmd = ["convert", "-loop", "0"]
    for i, p in enumerate(pngs):
        cmd += ["-delay", "90" if i == 0 else "55", p]
    cmd += ["-layers", "Optimize", args.out]
    subprocess.run(cmd, check=True)

    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass

    print("wrote %s from %d frames" % (args.out, len(pngs)))


if __name__ == "__main__":
    main()
