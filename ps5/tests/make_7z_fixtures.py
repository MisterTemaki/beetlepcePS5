#!/usr/bin/env python3
"""Makes the .7z archives the 7z tests use (tests/7z/, committed: the tests need no network and no py7zr), from the
port's own test card (make_test_rom.py). Needs py7zr (pip install py7zr).

  make_7z_fixtures.py <out-dir>

  lzma.7z        Card (USA).pce, LZMA                  lzma2.7z    the same, LZMA2
  ppmd.7z        the same, PPMd                        bcj.7z      the same, x86 BCJ + LZMA2
  delta.7z       the same, Delta + LZMA2
  solid.7z       readme.txt, Alpha (USA).pce, Beta (Japan).sgx in one LZMA2 block: the first HuCard is Alpha
  sgx.7z         notes/readme.txt, then Super (Japan).sgx: a SuperGrafx card after a text file
  header.7z      Card (USA).pce with a 512-byte copier header (its CRC is read, not the directory's)
  empty.7z       no files at all                       nogame.7z   readme.txt only
  disc.7z        Disc (Japan).cue + Disc (Japan).bin: a CD image, not listed
  bzip2.7z       Card (USA).pce, BZip2: a method the decoder doesn't have
  aes.7z         Card (USA).pce, AES-256 (password "pce"), names readable
  aeshdr.7z      the same with the names encrypted too
  big.7z         Big (USA).pce of 17 MiB: over the 16 MiB a HuCard may be
  bigblock.7z    Card (USA).pce then 65 MiB of padding in one solid block: over the 64 MiB block limit
  truncated.7z   lzma2.7z cut in half                  corrupt.7z  lzma2.7z with its packed data changed (CRC error)
  named.7z       splat.pce: the card with its CRC made Splatterhouse (USA)'s (forge_crc.py), named by the directory's CRC
  aesbig.7z      Big (USA).pce of 17 MiB, AES: refused as encrypted (not as too big)
  ppmdbig.7z     Card (USA).pce, PPMd with a 256 MiB model: over the decoder's 136 MiB budget
  many.7z        70,000 empty files, then Card (USA).pce: more files than an archive may list (65,536)
"""
import os
import subprocess
import sys

import py7zr

HERE = os.path.dirname(os.path.abspath(__file__))

# py7zr writes PPMd's properties as 7 bytes (order, memory, two zero bytes); 7-Zip writes 5, and the LZMA SDK's C decoder
# takes only 5. The stream itself is the same PPMd (variant H, 7z's range coder): write the properties as 7-Zip does.
_ppmd_props = py7zr.compressor.PpmdCompressor.encode_filter_properties.__func__


def _ppmd_props_7zip(cls, f):
    return _ppmd_props(cls, f)[:5]


py7zr.compressor.PpmdCompressor.encode_filter_properties = classmethod(_ppmd_props_7zip)
_ppmd_decode = py7zr.compressor.PpmdCompressor._decode_property


def _ppmd_decode_7zip(self, properties):
    return _ppmd_decode(self, properties + b"\0\0" if len(properties) == 5 else properties)


py7zr.compressor.PpmdCompressor._decode_property = _ppmd_decode_7zip


def card(path, kind="plain"):
    subprocess.run([sys.executable, os.path.join(HERE, "make_test_rom.py"), path, kind], check=True, stdout=subprocess.DEVNULL)
    with open(path, "rb") as f:
        return f.read()


def make(out, files, filters=None, password=None, header_encryption=False):
    if os.path.exists(out):
        os.remove(out)
    with py7zr.SevenZipFile(out, "w", filters=filters, password=password, header_encryption=header_encryption) as z:
        for name, data in files:
            z.writestr(data, name)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    tmp = os.path.join(out, "tmp.pce")
    rom = card(tmp)
    hdr = card(tmp, "header")
    card(tmp)
    with open(os.path.join(HERE, "..", "frontend", "data", "nointro.tsv"), encoding="utf-8") as f:
        crc = next(l.split("\t")[1] for l in f if l.rstrip("\n").endswith("\tSplatterhouse (USA)") and l.startswith("pce\t"))
    subprocess.run([sys.executable, os.path.join(HERE, "forge_crc.py"), tmp, crc], check=True, stdout=subprocess.DEVNULL)
    with open(tmp, "rb") as f:
        splat = f.read()
    os.remove(tmp)
    readme = b"A test archive for Beetle PCE PS5.\n" * 4
    lz2 = [{"id": py7zr.FILTER_LZMA2, "preset": 9}]
    one = [("Card (USA).pce", rom)]
    make(os.path.join(out, "lzma.7z"), one, [{"id": py7zr.FILTER_LZMA}])
    make(os.path.join(out, "lzma2.7z"), one, lz2)
    make(os.path.join(out, "ppmd.7z"), one, [{"id": py7zr.FILTER_PPMD, "order": 6, "mem": 24}])
    make(os.path.join(out, "bcj.7z"), one, [{"id": py7zr.FILTER_X86}, {"id": py7zr.FILTER_LZMA2}])
    make(os.path.join(out, "delta.7z"), one, [{"id": py7zr.FILTER_DELTA}, {"id": py7zr.FILTER_LZMA2}])
    make(os.path.join(out, "solid.7z"), [("readme.txt", readme), ("Alpha (USA).pce", rom), ("Beta (Japan).sgx", rom)], lz2)
    make(os.path.join(out, "sgx.7z"), [("notes/readme.txt", readme), ("Super (Japan).sgx", rom)], lz2)
    make(os.path.join(out, "header.7z"), [("Card (USA).pce", hdr)], lz2)
    make(os.path.join(out, "named.7z"), [("splat.pce", splat)], lz2)
    make(os.path.join(out, "empty.7z"), [], lz2)
    make(os.path.join(out, "nogame.7z"), [("readme.txt", readme)], lz2)
    make(os.path.join(out, "disc.7z"), [("Disc (Japan).cue", b'FILE "Disc (Japan).bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'),
                                        ("Disc (Japan).bin", b"\0" * (2352 * 16))], lz2)
    make(os.path.join(out, "bzip2.7z"), one, [{"id": py7zr.FILTER_BZIP2}])
    make(os.path.join(out, "aes.7z"), one, [{"id": py7zr.FILTER_LZMA2}, {"id": py7zr.FILTER_CRYPTO_AES256_SHA256}], password="pce")
    make(os.path.join(out, "aeshdr.7z"), one, [{"id": py7zr.FILTER_LZMA2}, {"id": py7zr.FILTER_CRYPTO_AES256_SHA256}],
         password="pce", header_encryption=True)
    make(os.path.join(out, "big.7z"), [("Big (USA).pce", rom + b"\xff" * (17 * 1024 * 1024 - len(rom)))], lz2)
    make(os.path.join(out, "aesbig.7z"), [("Big (USA).pce", rom + b"\xff" * (17 * 1024 * 1024 - len(rom)))],
         [{"id": py7zr.FILTER_LZMA2}, {"id": py7zr.FILTER_CRYPTO_AES256_SHA256}], password="pce")
    make(os.path.join(out, "ppmdbig.7z"), one, [{"id": py7zr.FILTER_PPMD, "order": 6, "mem": 28}])
    make(os.path.join(out, "many.7z"), [("%05d.txt" % i, b"") for i in range(70000)] + one, lz2)
    make(os.path.join(out, "bigblock.7z"), [("Card (USA).pce", rom), ("padding.bin", b"\0" * (65 * 1024 * 1024))], lz2)
    with open(os.path.join(out, "lzma2.7z"), "rb") as f:
        good = f.read()
    with open(os.path.join(out, "truncated.7z"), "wb") as f:
        f.write(good[: len(good) // 2])
    bad = bytearray(good)
    # the packed data starts right after the 32-byte signature header: change a byte in the middle of it
    bad[32 + 40] ^= 0x55
    with open(os.path.join(out, "corrupt.7z"), "wb") as f:
        f.write(bad)
    for n in sorted(os.listdir(out)):
        print("%-14s %7d bytes" % (n, os.path.getsize(os.path.join(out, n))))


if __name__ == "__main__":
    main()
