#!/usr/bin/env python3
"""
podglad.py -- zrzuca zawartosc ekranu e-paper z ESP32 do pliku PNG.

Firmware z DEBUG_FB_DUMP=1 wysyla po kazdym pelnym odswiezeniu bufor obrazu
zakodowany w base64. Ten skrypt go odbiera i zapisuje podglad 1:1, dzieki
czemu layout mozna ogladac na komputerze bez patrzenia na panel.

    python tools/podglad.py --port COM31 --out podglad.png

Opcje:
    --timeout N   ile sekund czekac na zrzut (domyslnie 90)
    --frames N    ile kolejnych zrzutow zapisac (domyslnie 1)
"""
import argparse
import base64
import sys
import time

import serial
from PIL import Image

BEGIN = "---FB-BEGIN"
END = "---FB-END---"


def say(*parts):
    """Konsola Windows bywa w cp1250 -- nie wywracaj sie na obcym znaku."""
    enc = sys.stdout.encoding or "utf-8"
    line = " ".join(str(p) for p in parts)
    sys.stdout.write(line.encode(enc, "replace").decode(enc, "replace") + "\n")


def capture(port, baud, out, timeout, frames):
    say(f"[i] otwieram {port} @ {baud} (otwarcie portu resetuje ESP32)")
    ser = serial.Serial(port, baud, timeout=1)
    # przeladowanie plytki: impuls na EN przez linie DTR/RTS
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.12)
    ser.setRTS(False)
    ser.reset_input_buffer()

    t0 = time.time()
    saved = 0
    collecting = False
    b64, w, h = [], 800, 480

    while time.time() - t0 < timeout and saved < frames:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").rstrip("\r\n")

        if line.startswith(BEGIN):
            parts = line.replace("-", " ").split()
            try:
                w, h = int(parts[-2]), int(parts[-1])
            except (ValueError, IndexError):
                w, h = 800, 480
            b64, collecting = [], True
            say(f"[i] odbieram klatke {w}x{h} ...")
            continue

        if line.startswith(END):
            collecting = False
            data = base64.b64decode("".join(b64))
            need = (w // 8) * h
            if len(data) < need:
                say(f"[!] za malo danych: {len(data)}/{need} B -- pomijam")
                continue
            # w buforze 1 = czarny; PIL w trybie '1' traktuje 1 jako bialy
            inv = bytes((~b) & 0xFF for b in data[:need])
            img = Image.frombytes("1", (w, h), inv)
            name = out if frames == 1 else out.replace(".png", f"_{saved + 1}.png")
            img.save(name)
            say(f"[OK] zapisano {name}  ({len(data)} B)")
            saved += 1
            continue

        if collecting:
            b64.append(line.strip())
        else:
            say("   ", line)

    ser.close()
    if saved == 0:
        say("[!] nie odebrano zadnego zrzutu -- sprawdz DEBUG_FB_DUMP i predkosc portu")
        return 1
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM31")
    ap.add_argument("--baud", type=int, default=460800)
    ap.add_argument("--out", default="podglad.png")
    ap.add_argument("--timeout", type=float, default=90)
    ap.add_argument("--frames", type=int, default=1)
    a = ap.parse_args()
    sys.exit(capture(a.port, a.baud, a.out, a.timeout, a.frames))
