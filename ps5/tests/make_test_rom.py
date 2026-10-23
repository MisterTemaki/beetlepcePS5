#!/usr/bin/env python3
"""Builds a tiny PC Engine test HuCard for the host tests (no game data needed).

The card shows a red screen; while the pad's button II is held (Cross on the PS5 pad, by default) it turns green.
So a de-tiled flip shows whether the whole chain works: the core's HuC6280, VDC and VCE, the port's scaler and
tiler, and the pad -> core input mapping (through the multitap's reset sequence, as games read it). At start it
also writes 0x5A into the backup RAM (bank 0xF7), and erases it while II is held, so the backup-RAM save is
exercised both ways.

    make_test_rom.py out.<pce|sgx> [plain|header|reversed|big:<KiB>|cdplay]

  plain     a 32 KiB card
  header    the same with a 512-byte copier header in front
  reversed  a TurboGrafx-16 dump with its data lines reversed (every byte's bits in the opposite order)
  big:<KiB> a card of that size (the code in its first bank, the rest 0xFF)
  cdplay    as a System Card: before the red screen it asks the CD drive (the SCSI bus at $1800) to play track 2 in a
            loop (NEC commands D8 and D9), so the disc is read while the game runs (tests/make_test_cd.py's tones)

The same card serves as a stand-in System Card for the CD tests (syscard3.pce): the core then boots the CD with it,
and the red screen shows that the disc, its sheet and the card were all loaded.
"""
import sys


def assemble(base, items):
    """items: bytes, ("label", name), (opcode_bytes, "label") for an 8-bit relative branch, or ("abs", op, name)
    for an instruction with a 16-bit absolute operand."""
    def size(it):
        if isinstance(it, tuple) and it[0] == "label":
            return 0
        if isinstance(it, tuple) and it[0] == "abs":
            return len(it[1]) + 2
        if isinstance(it, tuple):
            return len(it[0]) + 1
        return len(it)

    labels = {}
    pc = base
    for it in items:
        if isinstance(it, tuple) and it[0] == "label":
            labels[it[1]] = pc
        pc += size(it)
    out = bytearray()
    pc = base
    for it in items:
        if isinstance(it, tuple) and it[0] == "label":
            continue
        if isinstance(it, tuple) and it[0] == "abs":
            target = labels[it[2]]
            out += it[1] + bytes([target & 0xFF, target >> 8])
        elif isinstance(it, tuple):
            op, name = it
            off = labels[name] - (pc + len(op) + 1)
            assert -128 <= off <= 127, (name, off)
            out += op + bytes([off & 0xFF])
        else:
            out += it
        pc += size(it)
    return bytes(out), labels


def vdc(reg, value):
    # ST0 #reg; ST1 #lo; ST2 #hi
    return bytes([0x03, reg, 0x13, value & 0xFF, 0x23, value >> 8])


# The SCSI handshake with the CD drive, as the System Card does it: X = the command's offset in "cmds". Waits for the
# bus to be free, selects the drive, then answers each REQ: a command byte out (command phase), the status byte in
# (kept at $2100) or a message byte in; ACK ($1802 bit 7) until REQ drops. Returns when the drive frees the bus.
SCSI = [
    ("label", "scsi"),
    bytes([0xAD, 0x00, 0x18, 0x29, 0x80]),        # LDA $1800; AND #$80      BSY
    (bytes([0xD0]), "scsi"),                      # BNE scsi
    bytes([0x9C, 0x00, 0x21]),                    # STZ $2100                status = 0
    bytes([0xA9, 0x81, 0x8D, 0x01, 0x18]),        # LDA #$81; STA $1801
    bytes([0x8D, 0x00, 0x18]),                    # STA $1800                SEL
    ("label", "sc_loop"),
    bytes([0xAD, 0x00, 0x18, 0x29, 0x80]),        # LDA $1800; AND #$80
    (bytes([0xF0]), "sc_done"),                   # BEQ sc_done              bus free: the command is over
    bytes([0xAD, 0x00, 0x18, 0x29, 0x40]),        # LDA $1800; AND #$40      REQ
    (bytes([0xF0]), "sc_loop"),                   # BEQ sc_loop
    bytes([0xAD, 0x00, 0x18, 0x29, 0x38]),        # LDA $1800; AND #$38      MSG C/D I/O
    bytes([0xC9, 0x10]),                          # CMP #$10                 command phase
    (bytes([0xD0]), "sc_in"),                     # BNE sc_in
    ("abs", bytes([0xBD]), "cmds"),               # LDA cmds,X
    bytes([0x8D, 0x01, 0x18, 0xE8]),              # STA $1801; INX
    (bytes([0x80]), "sc_ack"),                    # BRA sc_ack
    ("label", "sc_in"),
    bytes([0xC9, 0x18]),                          # CMP #$18                 status phase
    (bytes([0xD0]), "sc_msg"),                    # BNE sc_msg
    bytes([0xAD, 0x01, 0x18, 0x8D, 0x00, 0x21]),  # LDA $1801; STA $2100
    (bytes([0x80]), "sc_ack"),                    # BRA sc_ack
    ("label", "sc_msg"),
    bytes([0xAD, 0x01, 0x18]),                    # LDA $1801                a message (or data) byte
    ("label", "sc_ack"),
    bytes([0xA9, 0x80, 0x8D, 0x02, 0x18]),        # LDA #$80; STA $1802      ACK
    ("label", "sc_wait"),
    bytes([0xAD, 0x00, 0x18, 0x29, 0x40]),        # LDA $1800; AND #$40
    (bytes([0xD0]), "sc_wait"),                   # BNE sc_wait              until REQ drops
    bytes([0x9C, 0x02, 0x18]),                    # STZ $1802
    (bytes([0x80]), "sc_loop"),                   # BRA sc_loop
    ("label", "sc_done"),
    bytes([0x60]),                                # RTS
    ("label", "cmds"),
    # D8: audio start position, play (1), by track (0x80): track 2 (BCD)
    bytes([0xD8, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80]),
    # D9: audio end position, loop (1), by track: the one after the last (= the end of the disc)
    bytes([0xD9, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80]),
]

# Plays track 2 in a loop (the fader set to full first, as the System Card does): each command again until its status is GOOD (the first one after power on reports the
# disc as changed).
CDPLAY = [
    bytes([0x9C, 0x0F, 0x18]),                    # STZ $180F                no fade: the CD's sound at full volume
    ("label", "cd_start"),
    bytes([0xA2, 0x00]),                          # LDX #0                   D8
    ("abs", bytes([0x20]), "scsi"),               # JSR scsi
    bytes([0xAD, 0x00, 0x21]),                    # LDA $2100
    (bytes([0xD0]), "cd_start"),                  # BNE cd_start
    ("label", "cd_end"),
    bytes([0xA2, 0x0A]),                          # LDX #10                  D9
    ("abs", bytes([0x20]), "scsi"),               # JSR scsi
    bytes([0xAD, 0x00, 0x21]),                    # LDA $2100
    (bytes([0xD0]), "cd_end"),                    # BNE cd_end
]


def card(cdplay=False):
    items = [
        bytes([0x78, 0xD8, 0xD4]),                    # SEI; CLD; CSH (7.16 MHz)
        bytes([0xA9, 0xFF, 0x53, 0x01]),              # LDA #$FF; TAM #$01   MPR0 = I/O page
        bytes([0xA9, 0xF8, 0x53, 0x02]),              # LDA #$F8; TAM #$02   MPR1 = RAM ($2000)
        bytes([0xA9, 0xF7, 0x53, 0x04]),              # LDA #$F7; TAM #$04   MPR2 = backup RAM ($4000)
        bytes([0xA2, 0xFF, 0x9A]),                    # LDX #$FF; TXS
        bytes([0x9C, 0x00, 0x04]),                    # STZ $0400            VCE: 5.37 MHz dot clock
        vdc(0x05, 0x0000),                            # CR = 0
        vdc(0x09, 0x0000),                            # MWR: 32x32 map
        vdc(0x0A, 0x0202),                            # HSR
        vdc(0x0B, 0x031F),                            # HDR: 32 tiles (256 dots)
        vdc(0x0C, 0x0F02),                            # VPR
        vdc(0x0D, 0x00EF),                            # VDW: 240 lines
        vdc(0x0E, 0x0003),                            # VCR
        vdc(0x07, 0x0000),                            # BXR
        vdc(0x08, 0x0000),                            # BYR
        vdc(0x05, 0x0080),                            # CR: background on (every tile is colour 0)
    ] + (CDPLAY if cdplay else []) + [
        bytes([0xA9, 0x5A, 0x8D, 0x10, 0x40]),        # LDA #$5A; STA $4010   backup RAM byte 0x10
        ("label", "loop"),
        bytes([0xA9, 0x01, 0x8D, 0x00, 0x10]),        # LDA #1; STA $1000     SEL
        bytes([0xA9, 0x03, 0x8D, 0x00, 0x10]),        # LDA #3; STA $1000     SEL + CLR: the multitap to pad 1
        bytes([0xA9, 0x01, 0x8D, 0x00, 0x10]),        # LDA #1; STA $1000     pad 1's directions
        bytes([0xA9, 0x00, 0x8D, 0x00, 0x10]),        # LDA #0; STA $1000     pad 1's buttons
        bytes([0xEA, 0xEA, 0xEA, 0xEA]),              # NOP x4
        bytes([0xA2, 0x38, 0xA0, 0x00]),              # LDX #$38; LDY #$00    red (GRB 0 7 0)
        bytes([0xAD, 0x00, 0x10]),                    # LDA $1000
        bytes([0x29, 0x02]),                          # AND #$02              II (0 = held)
        (bytes([0xD0]), "write"),                     # BNE write
        bytes([0xA2, 0xC0, 0xA0, 0x01]),              # LDX #$C0; LDY #$01    green (GRB 7 0 0)
        bytes([0x9C, 0x10, 0x40]),                    # STZ $4010             II held: the backup RAM byte erased
        ("label", "write"),
        bytes([0x9C, 0x02, 0x04, 0x9C, 0x03, 0x04]),  # STZ $0402; STZ $0403  colour 0
        bytes([0x8E, 0x04, 0x04, 0x8C, 0x05, 0x04]),  # STX $0404; STY $0405
        ("abs", bytes([0x4C]), "loop"),               # JMP loop
        ("label", "rti"),
        bytes([0x40]),                                # RTI
    ] + (SCSI if cdplay else [])
    code, labels = assemble(0xE000, items)
    bank = bytearray(b"\xFF" * 0x2000)
    bank[: len(code)] = code
    rti = labels["rti"]
    for vec in (0x1FF6, 0x1FF8, 0x1FFA, 0x1FFC):  # IRQ2, IRQ1, timer, NMI
        bank[vec] = rti & 0xFF
        bank[vec + 1] = rti >> 8
    bank[0x1FFE] = 0x00  # reset: $E000
    bank[0x1FFF] = 0xE0
    return bank


def reverse8(b):
    return int(f"{b:08b}"[::-1], 2)


def main():
    out = sys.argv[1]
    kind = sys.argv[2] if len(sys.argv) > 2 else "plain"
    size = 32 * 1024
    if kind.startswith("big:"):
        size = int(kind[4:]) * 1024
    rom = card(kind == "cdplay") + b"\xFF" * (size - 0x2000)
    if kind == "reversed":
        rom = bytes(reverse8(b) for b in rom)
    if kind == "header":
        rom = b"\x00" * 512 + rom
    with open(out, "wb") as f:
        f.write(rom)
    print(f"{out}: {len(rom)} bytes ({kind})")


if __name__ == "__main__":
    main()
