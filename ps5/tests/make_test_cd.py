#!/usr/bin/env python3
"""A stand-in PC Engine CD for the host tests: a data track (zeros), then an audio track of two known tones -- 1000 Hz
on the left, 1500 Hz on the right -- so the tests can check the CD audio that comes out of the app: its pitch, its
channels, its byte order and its level. The test card made with `make_test_rom.py <syscard> cdplay` stands in for the
System Card and plays track 2 in a loop, so the disc is read while the game runs (from a share: through netfs).

  make_test_cd.py <out> [--long]

--long makes the audio track 16 s long (instead of 4 s): more than the first megabyte of a track the app fetches
ahead for a disc on a share.

The output's extension picks the layout:
  .cue   one .bin of raw 2352-byte sectors, both tracks, and the sheet
  .chd   a CHD (version 5, uncompressed: libchdr reads it as it reads chdman's) with the CD metadata (CHT2)
The tracks' lengths are multiples of 4 frames, as a CHD pads them. (Genesis Plus GX PS5's tests/make_test_cd.py, with
a PC Engine disc's data track.)
"""
import math
import os
import struct
import sys

DATA_FRAMES = 160   # >= 150: 2 s of data track before the audio
AUDIO_FRAMES = 300  # 4 s of sound
RATE = 44100
TONES = (1000.0, 1500.0)
AMPLITUDE = 12000


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


def raw_data(lba, user):
    m, s, f = (lba + 150) // 4500, ((lba + 150) // 75) % 60, (lba + 150) % 75
    return b"\x00" + b"\xff" * 10 + b"\x00" + bytes([bcd(m), bcd(s), bcd(f), 1]) + user + b"\x00" * 288


def data_track():
    return b"".join(raw_data(i, b"\x00" * 2048) for i in range(DATA_FRAMES))


def audio_samples():
    """little-endian 16-bit stereo, 588 frames per sector"""
    out = bytearray()
    for i in range(AUDIO_FRAMES * 588):
        l = int(round(AMPLITUDE * math.sin(2 * math.pi * TONES[0] * i / RATE)))
        r = int(round(AMPLITUDE * math.sin(2 * math.pi * TONES[1] * i / RATE)))
        out += struct.pack("<hh", l, r)
    return bytes(out)


def msf(frames):
    return "%02d:%02d:%02d" % (frames // 4500, (frames // 75) % 60, frames % 75)


def chd(path, data, audio):
    unit = 2448
    hunk_frames = 8
    hunkbytes = unit * hunk_frames
    frames = []
    for i in range(DATA_FRAMES):
        frames.append(data[i * 2352:(i + 1) * 2352] + b"\x00" * 96)
    for i in range(AUDIO_FRAMES):
        sec = audio[i * 2352:(i + 1) * 2352]
        # chdman keeps CD audio big-endian: every 16-bit sample swapped
        swapped = bytearray(sec)
        swapped[0::2], swapped[1::2] = sec[1::2], sec[0::2]
        frames.append(bytes(swapped) + b"\x00" * 96)
    total = len(frames)
    hunks = (total + hunk_frames - 1) // hunk_frames
    meta = [
        "TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:%d PREGAP:0 PGTYPE:MODE1 PGSUB:RW POSTGAP:0" % DATA_FRAMES,
        "TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:%d PREGAP:0 PGTYPE:AUDIO PGSUB:RW POSTGAP:0" % AUDIO_FRAMES,
    ]
    hdr_len = 124
    # layout: header | metadata entries | map | (padding to a hunk boundary) | hunks
    meta_blobs = []
    off = hdr_len
    meta_offsets = []
    for text in meta:
        body = text.encode() + b"\x00"
        meta_offsets.append(off)
        off += 16 + len(body)
        meta_blobs.append(body)
    map_off = off
    off += 4 * hunks
    first_hunk = (off + hunkbytes - 1) // hunkbytes
    out = bytearray()
    out += b"MComprHD" + struct.pack(">II", hdr_len, 5) + struct.pack(">IIII", 0, 0, 0, 0)
    out += struct.pack(">QQQ", total * unit, map_off, meta_offsets[0])
    out += struct.pack(">II", hunkbytes, unit)
    out += b"\x00" * 60  # raw SHA-1, SHA-1, parent SHA-1
    assert len(out) == hdr_len
    for i, body in enumerate(meta_blobs):
        nxt = meta_offsets[i + 1] if i + 1 < len(meta_blobs) else 0
        out += b"CHT2" + struct.pack(">I", (0x01 << 24) | len(body)) + struct.pack(">Q", nxt) + body
    assert len(out) == map_off
    for h in range(hunks):
        out += struct.pack(">I", first_hunk + h)
    out += b"\x00" * (first_hunk * hunkbytes - len(out))
    blob = b"".join(frames)
    blob += b"\x00" * (hunks * hunkbytes - len(blob))
    out += blob
    with open(path, "wb") as f:
        f.write(out)


def main():
    global AUDIO_FRAMES
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--long"):
        raise SystemExit(__doc__)
    if len(sys.argv) == 3:
        AUDIO_FRAMES = 1200
    out = sys.argv[1]
    base, ext = os.path.splitext(out)
    name = os.path.basename(base)
    data = data_track()
    audio = audio_samples()
    if ext == ".cue":
        with open(base + ".bin", "wb") as f:
            f.write(data + audio)
        with open(out, "w") as f:
            f.write(f'FILE "{name}.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
                    f'  TRACK 02 AUDIO\n    INDEX 01 {msf(DATA_FRAMES)}\n')
    elif ext == ".chd":
        chd(out, data, audio)
    else:
        raise SystemExit("unknown layout: " + ext)
    print(out)


if __name__ == "__main__":
    main()
