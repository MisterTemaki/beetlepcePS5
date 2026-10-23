#!/bin/bash
# Beetle PCE PS5 host tests: the real port code (main-boot, shims, frontend, the Beetle PCE core; the installer and the
# helper) on Linux, with the PS5 calls faked by host/sce_host.cpp. A scripted pad drives the app; flips are
# de-tiled to PPM and checked. A tiny test HuCard (tests/make_test_rom.py) shows a red screen, green while II
# (Cross) is held; it also stands in for the System Card in the CD tests.
set -u
cd "$(dirname "$0")/.."
BIN=$PWD/build/host/pce-ps5-host
INSTALLER=$PWD/build/host/pce-ps5-installer
HELPER=$PWD/build/host/pce-ps5-helper
# ports nobody listens on, so the app's ordinary runs find no helper and no ELF loader
export PCEPS5_HELPER_PORT=$((20000 + RANDOM % 10000)) PCEPS5_ELFLDR_PORT=$((30000 + RANDOM % 10000)) PCEPS5_JB_NO_OTHERS=1
# the debug logs are off by default (1.0.1): the tests read boot.log, so here a settings file without the key means on
export PCEPS5_LOGS_DEFAULT=1
CHECK="python3 $PWD/tests/check_ppm.py"
RECT="python3 $PWD/tests/check_rect.py"
ROM="python3 $PWD/tests/make_test_rom.py"
CD="python3 $PWD/tests/make_test_cd.py"
TONE="python3 $PWD/tests/check_tone.py"
WORK=${WORK:-$(mktemp -d)}
ONLY=${ONLY:-}
PASS=0
FAIL=0

# pad bits
CROSS=4000; CIRCLE=2000; TRIANGLE=1000; UP=10; DOWN=40; RIGHT=20; L2=100; R2=200; L3R3=6; OPTIONS=8
SQUARE=8000; L1=400; R1=800
L2UP=$(printf %x $((0x$L2 | 0x$UP))); L2DOWN=$(printf %x $((0x$L2 | 0x$DOWN))); L2R2=$(printf %x $((0x$L2 | 0x$R2)))
# pause menu: L3+R3, then Up from "Resume" wraps to "Quit Beetle PCE PS5"
# the shelf: Options, then Cross on "Quit"
SHELFQUIT_AT() { echo "$1:$OPTIONS;$(($1 + 2)):0;$(($1 + 10)):$CROSS;$(($1 + 12)):0"; }
# buttons pressed one after the other from frame $1, 8 frames apart
SEQ() { local f=$1 out="" b; shift; for b in "$@"; do out="$out;$f:$b;$((f + 2)):0"; f=$((f + 8)); done; echo "${out#;}"; }
QUITAT() { echo "$1:$L3R3;$(($1 + 2)):0;$(($1 + 10)):$UP;$(($1 + 12)):0;$(($1 + 20)):$CROSS;$(($1 + 22)):0"; }

ok() { echo "  ok: $*"; PASS=$((PASS + 1)); }
bad() { echo "  FAIL: $*"; FAIL=$((FAIL + 1)); }
expect() { if eval "$1"; then ok "$2"; else bad "$2"; fi; }
want() { [ -z "$ONLY" ] || [[ " $ONLY " == *" $1 "* ]]; }

newroot() {
	local t=$WORK/$1
	rm -rf "$t" && mkdir -p "$t/root/roms" "$t/dump"
	echo "shader=0" >"$t/root/pce-ps5.ini" # the plain picture: the colour checks expect it (group 21 tests the shaders)
	echo "$t"
}

run() { # dir pad dumps [args...]
	local t=$1 pad=$2 dumps=$3
	shift 3
	PCEPS5_HOST_OFFLINE=${OFFLINE-1} PCEPS5_COVER_URL=${COVER_URL-} \
	PCEPS5_ROOT=$t/root PCEPS5_HOMEBREW=$t/homebrew PCEPS5_HOST_DUMP_DIR=$t/dump PCEPS5_HOST_DUMP=$dumps \
		PCEPS5_HOST_MAX_FLIPS=${MAXFLIPS-5000} PCEPS5_HOST_PAD=$pad ASAN_OPTIONS=detect_leaks=0 timeout 180 "$BIN" "$@" >"$t/out.txt" 2>&1
	echo $?
}
nosan() { expect "! grep -q 'runtime error\|AddressSanitizer' $1/out.txt" "no sanitizer reports"; }

system_test() { # name ext label [rom kind]
	local name=$1 ext=$2 label=$3
	local T
	T=$(newroot "$name")
	$ROM "$T/root/roms/test.$ext" ${4:-plain} >/dev/null
	local rc
	rc=$(run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 200)" "90,130" "$T/root/roms/test.$ext")
	expect "[ $rc = 0 ]" "$label: exit code 0 (got $rc)"
	expect "grep -q 'running \"test\" ($label)' $T/root/logs/boot.log" "$label: Beetle PCE runs it"
	expect "$RECT $T/dump/flip00090.ppm $T/root/logs/boot.log red >/dev/null" "$label: red picture where the log says, black around it"
	expect "$CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "$label: Cross -> green"
	nosan "$T"
	LAST=$T
}

if want 1; then
echo "== 1. a HuCard from the command line: picture, input, quick save (L2 + Up), quit from the pause menu"
T=$(newroot t1)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(run "$T" "0:0;100:$CROSS;140:0;200:$L2UP;205:0;$(QUITAT 220)" "90,130" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "$RECT $T/dump/flip00090.ppm $T/root/logs/boot.log red >/dev/null" "red picture where the log says, black around it"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null" "the card's screen is red"
expect "$CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "Cross -> II -> green"
expect "[ -f $T/root/states/PCEngine/test.state1 ]" "L2 + Up wrote states/PCEngine/test.state1"
expect "grep -q 'running \"test\" (PC Engine), 59.8' $T/root/logs/boot.log" "a PC Engine game at 59.8 Hz"
expect "grep -q 'picture 256x240' $T/root/logs/boot.log" "the 256-dot mode, 240 lines"
expect "grep -q 'save state 1: ok' $T/root/logs/boot.log" "logged the save"
expect "grep -q 'Fit to screen' $T/root/logs/boot.log" "fit to screen by default"
nosan "$T"
fi

if want 2; then
echo "== 2. SuperGrafx, a copier header, a TurboGrafx-16 dump with reversed data lines"
system_test t2sgx sgx "SuperGrafx"
system_test t2hdr pce "PC Engine" header
system_test t2rev pce "PC Engine" reversed
expect "grep -q 'data lines reversed: put right' $LAST/root/logs/boot.log" "the reversed dump was put right"
fi

if want 3; then
echo "== 3. shelf -> game -> back to the shelf -> quit"
T=$(newroot t3)
$ROM "$T/root/roms/Test Game (USA).pce" >/dev/null
mkdir -p "$T/root/roms/Action"
rc=$(run "$T" "0:0;40:$CROSS;42:0;150:$L3R3;152:0;160:$UP;162:0;164:$UP;166:0;170:$CROSS;172:0;200:$OPTIONS;202:0;210:$CROSS;212:0" "20,35,120,190")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "[ -f $T/dump/flip00020.ppm ]" "the shelf was shown"
expect "$CHECK $T/dump/flip00120.ppm 960 540 red >/dev/null" "the game picked on the shelf runs"
expect "grep -q 'last_rom=.*Test Game (USA).pce' $T/root/pce-ps5.ini" "the shelf remembers the last game"
expect "grep -q 'game closed' $T/root/logs/boot.log" "back to the list closed the game"
nosan "$T"
fi

if want 4; then
echo "== 4. a SuperGrafx game kept as .pce (the game table knows it) starts as one, named by its CRC"
T=$(newroot t4)
$ROM "$T/root/roms/ace.pce" >/dev/null
python3 tests/forge_crc.py "$T/root/roms/ace.pce" $(grep -P "^sgx\t[0-9A-F]{8}\tBattle Ace \(Japan\) \(En\)$" frontend/data/nointro.tsv | cut -f2) >/dev/null
rc=$(run "$T" "0:0;60:$CROSS;62:0;$(QUITAT 200)" "150")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'ace.pce \[sgx\] -> \"Battle Ace (Japan) (En)\" (by CRC)' $T/root/logs/boot.log" "the shelf names it Battle Ace, a SuperGrafx game"
expect "grep -q 'ace.pce is a SuperGrafx game: started as one' $T/root/logs/boot.log && grep -q 'running \"ace\" (SuperGrafx)' $T/root/logs/boot.log" "started as a SuperGrafx game"
expect "$CHECK $T/dump/flip00150.ppm 960 540 red >/dev/null" "it runs"
nosan "$T"
fi

if want 5; then
echo "== 5. CD games: without a System Card the screen names it; with one the disc starts; tracks, .m3u discs and .img aren't games"
T=$(newroot t5)
D=$T/root/roms/PCEngineCD
mkdir -p "$D/Multi" "$T/root/roms/PCEngine"
head -c $((2352 * 150)) /dev/zero >"$D/Game (Japan) (Track 1).bin"
head -c $((2352 * 300)) /dev/zero >"$D/Game (Japan) (Track 2).bin"
cat >"$D/Game (Japan).cue" <<'CUE'
FILE "Game (Japan) (Track 1).bin" BINARY
  TRACK 01 AUDIO
    INDEX 01 00:00:00
FILE "Game (Japan) (Track 2).bin" BINARY
  TRACK 02 MODE1/2352
    INDEX 00 00:00:00
    INDEX 01 00:02:00
CUE
# a two-disc game: the .m3u is the game, its discs aren't listed again; a CloneCD image: the .ccd, not its .img/.sub
for n in 1 2; do
	head -c $((2352 * 300)) /dev/zero >"$D/Multi/Two (Disc $n).bin"
	printf 'FILE "Two (Disc %s).bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n' $n >"$D/Multi/Two (Disc $n).cue"
done
printf 'Two (Disc 1).cue\nTwo (Disc 2).cue\n' >"$D/Multi/Two.m3u"
head -c $((2352 * 300)) /dev/zero >"$D/Clone.img"; : >"$D/Clone.sub"; printf '[CloneCD]\nVersion=3\n' >"$D/Clone.ccd"
$ROM "$T/root/roms/PCEngine/Alpha (USA).pce" >/dev/null
# the shelf: Alpha, Clone, Game, Two; Right twice -> Game, Cross -> the System Card message, Cross -> back, Options -> quit
rc=$(run "$T" "0:0;30:$RIGHT;32:0;40:$RIGHT;42:0;50:$CROSS;52:0;90:$CROSS;92:0;110:$OPTIONS;112:0;120:$CROSS;122:0" "70")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '\[games\] 4 ROM(s)' $T/root/logs/boot.log" "the shelf lists the .pce, the .cue, the .m3u and the .ccd: not the tracks, the .m3u's discs or the .img"
expect "! grep -q 'Two (Disc 1).cue \[' $T/root/logs/boot.log && grep -q 'Two.m3u \[pcecd\]' $T/root/logs/boot.log" "the .m3u stands for its discs"
expect "grep -q 'missing firmware: no System Card' $T/root/logs/boot.log" "logged: no System Card"
expect "grep -q 'could not start .*Game (Japan).cue: no System Card' $T/root/logs/boot.log" "the game wasn't started"
expect "[ -f $T/dump/flip00070.ppm ]" "a message was on the screen"
# a stand-in System Card (the test card): the disc goes to the core, which starts the card with it
mkdir -p "$T/root/bios" && $ROM "$T/root/bios/syscard3.pce" >/dev/null
rc=$(run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 200)" "90,130" "$D/Game (Japan).cue")
expect "[ $rc = 0 ]" "with bios/syscard3.pce: exit code 0 (got $rc)"
expect "grep -q 'loading .*Game (Japan).cue, System Card syscard3.pce' $T/root/logs/boot.log && grep -q 'running \"Game (Japan)\" (PC Engine CD)' $T/root/logs/boot.log" "the disc starts with syscard3.pce"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null && $CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "the System Card runs, its pad works"
rc=$(PCEPS5_HOST_QUIT_AFTER=3 run "$T" "0:0" "" "$D/Multi/Two.m3u")
expect "grep -q 'running \"Two\" (PC Engine CD)' $T/root/logs/boot.log" "a two-disc .m3u starts"
# a card chosen in the settings that isn't there: named
echo "cd_bios=2" >>"$T/root/pce-ps5.ini"
rc=$(MAXFLIPS=300 run "$T" "0:0;60:$CROSS;62:0" "" "$D/Game (Japan).cue")
expect "grep -q 'could not start .*: no System Card' $T/root/logs/boot.log" "System Card 3.0 (US) chosen but missing: not started"
# Auto takes the best card there: 3.0 before 1.0
sed -i '/^cd_bios=/d' "$T/root/pce-ps5.ini"; mv "$T/root/bios/syscard3.pce" "$T/root/bios/syscard1.pce"
rc=$(PCEPS5_HOST_QUIT_AFTER=2 run "$T" "0:0" "" "$D/Game (Japan).cue")
expect "grep -q 'System Card syscard1.pce' $T/root/logs/boot.log" "Auto: syscard1.pce when it is the only one"
nosan "$T"
fi

if want 6; then
echo "== 6. integer scale and scanlines from the settings file; load state with L2 + Down"
T=$(newroot t6)
$ROM "$T/root/roms/test.pce" >/dev/null
printf 'scale=1\nscanlines=1\n' >>"$T/root/pce-ps5.ini"
rc=$(run "$T" "0:0;60:$L2UP;62:0;80:$L2DOWN;82:0;$(QUITAT 100)" "50" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'Integer scale, scanlines' $T/root/logs/boot.log" "integer scale with scanlines"
expect "$RECT $T/dump/flip00050.ppm $T/root/logs/boot.log red >/dev/null" "red picture where the log says"
expect "python3 - $T/dump/flip00050.ppm <<'PY'
import sys
d = open(sys.argv[1], 'rb').read().split(b'\n', 3)
w, h = map(int, d[1].split()); px = d[3]
reds = {px[(y * w + 960) * 3] for y in range(100, 980)}
top = max(reds)
sys.exit(0 if top >= 200 and any(0.35 * top <= r <= 0.65 * top for r in reds) else 1)
PY" "scanline rows are darker"
expect "grep -q 'load state 1: ok' $T/root/logs/boot.log" "L2 + Down loaded the state"
nosan "$T"
fi

if want 7; then
echo "== 7. a zipped game; the sound stays fed and near the 60 ms target"
T=$(newroot t7)
$ROM "$T/game.pce" >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED); z.write(sys.argv[2],'game.pce'); z.close()" "$T/root/roms/game.zip" "$T/game.pce"
rc=$(PCEPS5_HOST_REALTIME=1 run "$T" "0:0;$(QUITAT 760)" "100" "$T/root/roms/game.zip")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'loading .*game.zip -> game.pce' $T/root/logs/boot.log" "the game inside the zip is loaded"
expect "$CHECK $T/dump/flip00100.ppm 960 540 red >/dev/null" "the zipped game runs"
u2=$(grep -o 'audio queued .*underruns [0-9]*' "$T/root/logs/boot.log" | sed -n 2p | grep -o '[0-9]*$')
u3=$(grep -o 'audio queued .*underruns [0-9]*' "$T/root/logs/boot.log" | sed -n 3p | grep -o '[0-9]*$')
lat=$(grep -o 'audio queued [0-9]* ([0-9]* ms)' "$T/root/logs/boot.log" | sed -n 3p | grep -o '([0-9]*' | tr -d '(')
under=$(( ${u3:-999} - ${u2:-0} ))
expect "[ $under -le 2 ]" "audio underruns over 5 s of play: $under"
# the floor is low: a slow test machine (ASan, 2 cores) runs a little under 60 fps and drains the ring toward it
expect "[ ${lat:-0} -ge 15 ] && [ ${lat:-0} -le 120 ]" "audio latency ${lat:-?} ms"
nosan "$T"
fi

if want 8; then
echo "== 8. little video memory: 720p scan-out; memory busy at first: retries"
T=$(newroot t8)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(PCEPS5_HOST_DIRECT_MAX_MIB=12 run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'scan-out 1280x720' $T/root/logs/boot.log" "fell back to 1280x720"
expect "head -2 $T/dump/flip00090.ppm | grep -q '1280 720'" "the flips are 1280x720"
expect "$RECT $T/dump/flip00090.ppm $T/root/logs/boot.log red >/dev/null" "game picture in place (720p)"
T=$(newroot t8b)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(PCEPS5_HOST_DIRECT_FAIL_FIRST=9 run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'retrying (1)' $T/root/logs/boot.log && grep -q 'scan-out 1920x1080' $T/root/logs/boot.log" "retried, then got 1080p"
fi

if want 9; then
echo "== 9. BeetlePCEPS5.elf installs the app (icon and libc.prx included), stays as the helper, repairs"
T=$(newroot t9)
APP=$T/homebrew/PPSA99925
inst() { # dir -> runs the installer; it stays running as the helper when the port is free
	PCEPS5_ROOT=$1/root PCEPS5_HOMEBREW=$1/homebrew PCEPS5_APPMETA=$1/appmeta ASAN_OPTIONS=detect_leaks=0 timeout 60 "$INSTALLER" >>"$1/inst.txt" 2>&1
}
waitfor() { for i in $(seq 1 50); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 0.1; done; return 1; }
stop_helpers() { pkill -f 'build/host/pce-ps5-(installer|helper)' 2>/dev/null; pkill -f 'received.elf' 2>/dev/null; sleep 0.3; }
stop_helpers
inst "$T" &
expect "waitfor $T/root/logs/installer.log 'listening on 127.0.0.1'" "the installer stays running as the helper"
expect "cmp -s $APP/sce_sys/icon0.png app/sce_sys/icon0.png" "icon0.png installed (the Beetle PCE PS5 icon)"
expect "cmp -s $APP/sce_sys/pic0.dds app/sce_sys/pic0.dds && cmp -s $APP/sce_sys/pic1.dds app/sce_sys/pic1.dds && cmp -s $APP/sce_sys/param.json app/sce_sys/param.json" "pic0.dds/pic1.dds (background) and param.json installed"
expect "cmp -s $APP/eboot.bin tests/fake-eboot.bin" "eboot.bin installed"
expect "cmp -s $APP/sce_module/libc.prx tests/fake-libc.prx" "sce_module/libc.prx installed"
expect "grep -q 'Beetle PCE PS5 [0-9.]* installed. Open it from the Beetle PCE PS5 icon' $T/inst.txt" "install notification says to open the icon"
expect "! ls $APP/*.part $APP/sce_sys/*.part $APP/sce_module/*.part 2>/dev/null | grep -q ." "no .part files left"
inst "$T"
expect "grep -q 'is up to date' $T/root/logs/installer.log && ! grep -q 'wrote' $T/root/logs/installer.log" "sent again: nothing rewritten"
expect "grep -q 'a helper is already running' $T/root/logs/installer.log" "sent again: the second copy leaves the helper to the first"
echo broken > $APP/sce_sys/icon0.png
inst "$T"
expect "cmp -s $APP/sce_sys/icon0.png app/sce_sys/icon0.png" "a damaged icon is put back"
META=$T/appmeta/PPSA99925
mkdir -p "$META" && echo old > "$META/pic0.png" && echo old > "$META/icon0.png" && cp app/sce_sys/param.json "$META/"
echo old > "$APP/sce_sys/pic0.png"
inst "$T"
expect "cmp -s $META/pic0.dds app/sce_sys/pic0.dds && cmp -s $META/pic1.dds app/sce_sys/pic1.dds && cmp -s $META/icon0.png app/sce_sys/icon0.png" "appmeta gets the background (pic0/pic1.dds) and icon"
expect "[ ! -f $META/pic0.png ] && [ ! -f $APP/sce_sys/pic0.png ]" "an old pic0.png is removed"
expect "grep -q 'Home screen art updated' $T/inst.txt" "the notification says the home screen art changed"
stop_helpers
expect "[ \$(find $T -path '*PPSA9992[234]*' -o -path '*PPSA9900[0-9]*' -o -path '*PPSA9901[0-9]*' -o -path '*PPSA99203*' | wc -l) = 0 ]" "nothing written for the other ports (PPSA99922-24, their old PPSA99009-12) or PS5SX2 (PPSA99203)"
fi

if want 10; then
echo "== 10. covers per system: download, git-symlink, name by CRC, your own cover first, 404 remembered"
T=$(newroot t10)
PCEREPO=NEC_-_PC_Engine_-_TurboGrafx_16; CDREPO=NEC_-_PC_Engine_CD_-_TurboGrafx-CD
mkdir -p "$T/srv/$PCEREPO/Named_Boxarts" "$T/srv/$CDREPO/Named_Boxarts" "$T/root/covers/PCEngine" "$T/root/roms/PCEngine" "$T/root/roms/PCEngineCD"
python3 - "$T/srv" "$T/root/covers" "$PCEREPO" "$CDREPO" <<'PY'
import sys
from PIL import Image
srv, covers, pce, cd = sys.argv[1:5]
m, s = f"{srv}/{pce}/Named_Boxarts", f"{srv}/{cd}/Named_Boxarts"
Image.new('RGB', (512, 357), (255, 0, 0)).save(m + "/R-Type (USA).png")
Image.new('RGB', (512, 357), (0, 255, 0)).save(m + "/Splatterhouse (Japan).png")
open(m + "/Splatterhouse (USA).png", 'w').write("Splatterhouse (Japan).png")
Image.new('RGB', (512, 357), (255, 255, 0)).save(m + "/Blazing Lazers (USA).png")
Image.new('RGB', (360, 512), (0, 0, 255)).save(covers + "/PCEngine/Blazing Lazers (USA).png")
Image.new('RGB', (360, 512), (255, 0, 255)).save(s + "/Akumajou Dracula X - Chi no Rondo (Japan).png")
PY
for n in "R-Type (USA)" "Bomberman '93 (USA)" "Blazing Lazers (USA)"; do $ROM "$T/root/roms/PCEngine/$n.pce" >/dev/null; done
head -c $((2352 * 300)) /dev/zero >"$T/root/roms/PCEngineCD/Akumajou Dracula X - Chi no Rondo (Japan).bin"
printf 'FILE "Akumajou Dracula X - Chi no Rondo (Japan).bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n' >"$T/root/roms/PCEngineCD/Akumajou Dracula X - Chi no Rondo (Japan).cue"
$ROM "$T/root/roms/PCEngine/splat.pce" >/dev/null
python3 tests/forge_crc.py "$T/root/roms/PCEngine/splat.pce" $(grep -P "^pce\t[0-9A-F]{8}\tSplatterhouse \(USA\)$" frontend/data/nointro.tsv | cut -f2) >/dev/null
PORT=18080
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
# shelf order: Akumajou Dracula X, Blazing Lazers, Bomberman '93, R-Type, Splatterhouse
rc=$(OFFLINE= COVER_URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png" PCEPS5_HOST_REALTIME=1 run "$T" \
	"0:0;180:$RIGHT;182:0;230:$RIGHT;232:0;280:$RIGHT;282:0;330:$RIGHT;332:0;380:$OPTIONS;382:0;390:$CROSS;392:0" "170,220,270,320,370")
kill $SRVPID 2>/dev/null
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'splat.pce \[pce\] -> \"Splatterhouse (USA)\" (by CRC)' $T/root/logs/boot.log" "splat.pce named by its CRC"
expect "[ -f '$T/root/covers/PCEngine/R-Type (USA).png' ]" "a HuCard cover saved in covers/PCEngine"
expect "[ -f '$T/root/covers/PCEngineCD/Akumajou Dracula X - Chi no Rondo (Japan).png' ]" "a CD cover from its repository, in covers/PCEngineCD"
expect "[ -f \"$T/root/covers/PCEngine/Bomberman '93 (USA).missing\" ]" "a 404 is remembered (.missing)"
expect "grep -q 'is a link to Splatterhouse (Japan).png' $T/root/logs/boot.log" "a git-symlink cover is followed"
expect "! grep -q 'GET .*Blazing%20Lazers' $T/root/logs/boot.log" "your own cover (covers/PCEngine/<ROM name>.png): nothing downloaded for it"
expect "$CHECK $T/dump/flip00170.ppm 960 420 255 0 255 >/dev/null" "Dracula X shows the CD (magenta) cover"
expect "$CHECK $T/dump/flip00220.ppm 960 420 0 0 255 >/dev/null" "Blazing Lazers shows your own (blue) cover, not the server's"
expect "! $CHECK $T/dump/flip00270.ppm 960 420 255 0 0 >/dev/null 2>&1 && ! $CHECK $T/dump/flip00270.ppm 960 420 0 255 0 >/dev/null 2>&1" "no cover online: a placeholder card"
# 1.1.1 (genplusgxPS5 issue #4): the card's emblem, top right, is the PC Engine pad's two round buttons (II, I) on a
# light plate, no longer the SNES pad's four coloured buttons (red, yellow, green, blue: sampled where they were)
P=$T/dump/flip00270.ppm
expect "! $CHECK $P 1201 227 225 60 70 40 >/dev/null && ! $CHECK $P 1238 195 240 200 60 40 >/dev/null && ! $CHECK $P 1275 227 70 180 90 40 >/dev/null && ! $CHECK $P 1238 258 70 110 230 40 >/dev/null" "the placeholder card no longer shows the SNES pad's four coloured buttons"
expect "$CHECK $P 1201 227 36 36 44 12 >/dev/null && $CHECK $P 1275 227 36 36 44 12 >/dev/null && $CHECK $P 1238 195 200 200 212 16 >/dev/null && $CHECK $P 1238 227 200 200 212 16 >/dev/null" "it shows the PC Engine pad's two round buttons (II, I) on their plate"
expect "$CHECK $T/dump/flip00320.ppm 960 420 255 0 0 >/dev/null" "R-Type shows the downloaded (red) cover"
expect "$CHECK $T/dump/flip00370.ppm 960 420 0 255 0 >/dev/null" "Splatterhouse shows the linked (green) cover"
nosan "$T"
fi

if want 11; then
echo "== 11. offline: no download is tried, the shelf still works"
T=$(newroot t11)
$ROM "$T/root/roms/R-Type (USA).pce" >/dev/null
rc=$(run "$T" "0:0;30:$OPTIONS;32:0;40:$CROSS;42:0" "20")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'not connected' $T/root/logs/boot.log && ! grep -q 'GET ' $T/root/logs/boot.log" "no request made without a network"
fi

if want 12; then
echo "== 12. the shelf's tabs: Down shows one system; the game picked there runs"
T=$(newroot t12)
$ROM "$T/root/roms/Alpha (USA).sgx" >/dev/null
$ROM "$T/root/roms/Beta (USA).pce" >/dev/null
head -c $((2352 * 300)) /dev/zero >"$T/root/roms/Gamma (Japan).bin"
printf 'FILE "Gamma (Japan).bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n' >"$T/root/roms/Gamma (Japan).cue"
rc=$(run "$T" "0:0;30:$DOWN;32:0;60:$CROSS;62:0;$(QUITAT 160)" "25,55,150")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '\[shelf\] tab PC Engine: 1 game(s)' $T/root/logs/boot.log" "Down: the PC Engine tab, one game"
expect "grep -q 'loading .*Beta (USA).pce' $T/root/logs/boot.log" "the PC Engine game was started from its tab"
expect "grep -q 'shelf_family=1' $T/root/pce-ps5.ini" "the tab is remembered"
expect "$CHECK $T/dump/flip00150.ppm 960 540 red >/dev/null" "the PC Engine game runs"
nosan "$T"
fi

if want 13; then
echo "== 13. settings from the shelf (Triangle); fast forward (R2) and rewind (L2 + R2); the pause menu saves a state"
T=$(newroot t13)
$ROM "$T/root/roms/Alpha (USA).pce" >/dev/null
# Triangle -> settings ("Shader" first, Down -> "Screen size"), Right -> integer scale, Circle -> back; Cross -> play
rc=$(PCEPS5_HOST_REALTIME=1 run "$T" "0:0;30:$TRIANGLE;32:0;36:$DOWN;38:0;40:$RIGHT;42:0;50:$CIRCLE;52:0;70:$CROSS;72:0;150:$R2;250:0;300:$L2R2;340:0;360:$L3R3;362:0;370:$DOWN;372:0;380:$CROSS;382:0;400:$CIRCLE;402:0;$(QUITAT 440)" "45")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'scale=1' $T/root/pce-ps5.ini" "the settings screen changed the screen size"
expect "grep -q 'fast forward on' $T/root/logs/boot.log && grep -q 'fast forward off' $T/root/logs/boot.log" "R2 held: fast forward"
expect "grep -q 'rewind on' $T/root/logs/boot.log && grep -q 'rewind off' $T/root/logs/boot.log" "L2 + R2 held: rewind"
expect "[ -f '$T/root/states/PCEngine/Alpha (USA).state1' ]" "the pause menu's Save state wrote slot 1"
nosan "$T"
fi

if want 14; then
echo "== 14. the pad is shared with the system (handle 0x809b0081, as on the console)"
T=$(newroot t14)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(PCEPS5_HOST_PAD_SHARED=1 PCEPS5_HOST_REALTIME=1 run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 300)" "130" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'using 809b0081 (shared with the system)' $T/root/logs/boot.log" "the system's handle is used"
expect "[ \$(grep -c 'scePadOpen' $T/root/logs/boot.log) = 1 ]" "opened once, not every 2 seconds"
expect "$CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "its buttons reach the game (Cross -> green)"
fi

if want 15; then
echo "== 15. the app asks the helper to let it out of the sandbox"
waitfor() { for i in $(seq 1 50); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 0.1; done; return 1; }
stop_helpers() { pkill -f 'build/host/pce-ps5-(installer|helper)' 2>/dev/null; pkill -f 'received.elf' 2>/dev/null; sleep 0.3; }
T=$(newroot t15)
$ROM "$T/root/roms/test.pce" >/dev/null
QUIT=$(QUITAT 30)
PCEPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
rc=$(run "$T" "$QUIT" "" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'Beetle PCE helper (port [0-9]*): ret 0' $T/root/logs/boot.log" "the helper said yes"
expect "grep -q 'jailbreak: Beetle PCE helper' $T/root/logs/boot.log" "logged before /data was open, written to boot.log"
expect "grep -q 'letting it out' $T/hroot/logs/helper.log" "the helper let the app's process out"
stop_helpers
T=$(newroot t15b)
$ROM "$T/root/roms/test.pce" >/dev/null
PCEPS5_HOST_JB_TITLE=PPSA99921 PCEPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
rc=$(run "$T" "$QUIT" "" "$T/root/roms/test.pce")
expect "grep -q 'title PPSA99921 is not Beetle PCE PS5' $T/hroot/logs/helper.log" "the helper lets out no other title (not even Genesis Plus GX PS5)"
stop_helpers

echo "== 16. no helper running: the app hands its own helper to the ELF loader, then asks it"
T=$(newroot t16)
$ROM "$T/root/roms/test.pce" >/dev/null
python3 - "$PCEPS5_ELFLDR_PORT" "$T" <<'PY' &
import os, socket, subprocess, sys
port, t = int(sys.argv[1]), sys.argv[2]
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', port)); s.listen(1); s.settimeout(60)
c, _ = s.accept()
data = b''
while True:
    d = c.recv(65536)
    if not d: break
    data += d
path = t + '/received.elf'
open(path, 'wb').write(data); os.chmod(path, 0o755)
env = dict(os.environ, PCEPS5_ROOT=t + '/hroot', ASAN_OPTIONS='detect_leaks=0')
p = subprocess.Popen(['timeout', '30', path], env=env, stdout=open(t + '/helper.txt', 'w'), stderr=subprocess.STDOUT)
open(t + '/helper.pid', 'w').write(str(p.pid))
PY
LPID=$!
sleep 0.5
rc=$(run "$T" "$QUIT" "" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "cmp -s $T/received.elf $HELPER" "the ELF loader got the helper built into the app"
expect "grep -q 'jailbreak: Beetle PCE helper (started by the app)' $T/root/logs/boot.log" "then the helper it started let it out"
wait $LPID 2>/dev/null
stop_helpers
fi

if want 17; then
echo "== 17. no /data even so: the screen says what to do; the screen busy at first: retries"
T=$(newroot t17)
rm -rf "$T/root" && echo "not a folder" >"$T/root" # the app can't create its folders there
rc=$(run "$T" "0:0;30:$CROSS;32:0" "20")
expect "[ $rc = 2 ]" "exit code 2 (got $rc)"
expect "grep -q 'no access to /data' $T/out.txt" "logged: no access to /data"
expect "[ -f $T/dump/flip00020.ppm ]" "a message was on the screen"
T=$(newroot t17b)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(PCEPS5_HOST_VIDEO_BUSY=3 run "$T" "$(QUITAT 30)" "" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "[ \$(grep -c 'sceVideoOutOpen -> .*80290009' $T/root/logs/boot.log) = 3 ] && grep -q 'scan-out 1920x1080' $T/root/logs/boot.log" "VideoOut busy three times, then opened"
expect "grep -q 'splash screen hidden' $T/root/logs/boot.log" "the splash screen is hidden"
fi

if want 18; then
echo "== 18. covers in the background: the helper downloads them while the shelf runs; an older helper: the prefetch"
waitfor() { for i in $(seq 1 50); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 0.1; done; return 1; }
stop_helpers() { pkill -f 'build/host/pce-ps5-(installer|helper)' 2>/dev/null; pkill -f 'received.elf' 2>/dev/null; sleep 0.3; }
stop_helpers
T=$(newroot t18)
REPO=NEC_-_PC_Engine_-_TurboGrafx_16
SRV=$T/srv/$REPO/Named_Boxarts; mkdir -p "$SRV" "$T/root/covers"
COVER="R-Type (USA).png"
python3 -c "from PIL import Image; Image.new('RGB',(512,357),(255,0,0)).save('$SRV/$COVER')"
$ROM "$T/root/roms/R-Type (USA).pce" >/dev/null
PORT=18081
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
PCEPS5_ROOT=$T/root ASAN_OPTIONS=detect_leaks=0 timeout 120 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/root/logs/helper.log" "listening" || sleep 1
URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png"
rc=$(OFFLINE= COVER_URL="$URL" PCEPS5_HOST_REALTIME=1 run "$T" "0:0;300:$OPTIONS;302:0;310:$CROSS;312:0" "20,280")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'the helper downloads the covers while the app runs: nothing to wait for' $T/root/logs/boot.log && ! grep -q 'restarting' $T/root/logs/boot*.log" "the app starts at once: no download before /data, no restart"
expect "grep -q 'PCEngine/R-Type (USA).png' $T/root/covers/wanted.txt" "the missing cover goes to covers/wanted.txt"
expect "grep -q 'PCEngine/R-Type (USA).png -> 200' $T/root/logs/helper.log && cmp -s '$T/root/covers/PCEngine/$COVER' '$SRV/$COVER'" "the helper downloaded it into covers/PCEngine"
expect "! $CHECK $T/dump/flip00020.ppm 960 420 255 0 0 >/dev/null 2>&1 && $CHECK $T/dump/flip00280.ppm 960 420 255 0 0 >/dev/null" "the shelf showed its card, then the cover once it landed"
expect "grep -q '^0 1 idle' $T/root/covers/progress.txt" "covers/progress.txt: nothing left, 1 fetched"
kill $SRVPID 2>/dev/null
stop_helpers
# another daemon answering in the old way (the list without "covers: background"): the prefetch before /data
T=$(newroot t18b)
mkdir -p "$T/srv/$REPO/Named_Boxarts" "$T/root/covers"
cp "$SRV/$COVER" "$T/srv/$REPO/Named_Boxarts/"
$ROM "$T/root/roms/R-Type (USA).pce" >/dev/null
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
printf 'PCEngine/%s\thttp://127.0.0.1:%s/%s/Named_Boxarts/R-Type%%20%%28USA%%29.png\n' "$COVER" $PORT $REPO >"$T/wanted.txt"
python3 - "$PCEPS5_HELPER_PORT" "$T/wanted.txt" <<'PY' &
import socket, struct, sys, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', int(sys.argv[1]))); s.listen(4); s.settimeout(60)
text = open(sys.argv[2], 'rb').read()
end = time.time() + 60
while time.time() < end:
    try:
        c, _ = s.accept()
    except OSError:
        break
    req = b''
    while len(req) < 0xA10:
        d = c.recv(0xA10 - len(req))
        if not d: break
        req += d
    if len(req) == 0xA10:
        magic, cmd, pid, ret = struct.unpack_from('<IiiI', req, 0)
        out = bytearray(req)
        if cmd == 6:
            struct.pack_into('<i', out, 12, len(text)); c.sendall(bytes(out) + text)
        elif cmd == 5:
            struct.pack_into('<i', out, 12, 0); out[16:18] = b'ok'; c.sendall(bytes(out))
    c.close()
PY
OLDPID=$!
sleep 0.5
rc=$(OFFLINE= COVER_URL="$URL" run "$T" "0:0;30:$OPTIONS;32:0;40:$CROSS;42:0" "")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '\[prefetch\] 1 of 1 fetched' $T/root/logs/boot.log && cmp -s '$T/root/covers/PCEngine/$COVER' '$SRV/$COVER'" "an older helper: the cover is prefetched before /data and saved"
kill $OLDPID $SRVPID 2>/dev/null
stop_helpers
fi

if want 19; then
echo "== 19. the system folders: made in roms/ and covers/ at start"
T=$(newroot t19)
$ROM "$T/root/roms/R-Type (USA).pce" >/dev/null
rc=$(run "$T" "0:0;60:$OPTIONS;62:0;70:$CROSS;72:0" "50")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
for d in PCEngine SuperGrafx PCEngineCD; do
	expect "[ -d $T/root/roms/$d ] && [ -d $T/root/covers/$d ]" "roms/$d and covers/$d made"
done
nosan "$T"
fi

if want 20; then
echo "== 20. controls: the button layout, the pad type, the multitap and turbo (Settings, CONTROLS)"
T=$(newroot t20)
$ROM "$T/root/roms/test.pce" >/dev/null
# II moved from Cross to Triangle: Cross does nothing, Triangle is II (green)
printf 'btn_2=3\n' >>"$T/root/pce-ps5.ini"
rc=$(run "$T" "0:0;100:$CROSS;140:0;160:$TRIANGLE;200:0;$(QUITAT 220)" "130,190" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "$CHECK $T/dump/flip00130.ppm 960 540 red >/dev/null" "II on Triangle: Cross no longer presses II"
expect "$CHECK $T/dump/flip00190.ppm 960 540 green >/dev/null" "II on Triangle: Triangle presses II"
nosan "$T"
# the multitap off: the card still reads pad 1 (Cross -> green)
T=$(newroot t20b)
$ROM "$T/root/roms/test.pce" >/dev/null
printf 'multitap=0\n' >>"$T/root/pce-ps5.ini"
rc=$(run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 200)" "130" "$T/root/roms/test.pce")
expect "[ $rc = 0 ] && $CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "no multitap: II still reaches the game"
nosan "$T"
# the settings screen: Triangle, Down 23 times from Shader to Pad, Right (6 buttons), Down + Right (multitap off),
# Down + Right (turbo: switches), Down x3 to button II, Right (Circle), Circle, quit
T=$(newroot t20c)
$ROM "$T/root/roms/Alpha (USA).pce" >/dev/null
P="0:0;30:$TRIANGLE;32:0"; t=40
for i in $(seq 1 23); do P="$P;$t:$DOWN;$((t + 2)):0"; t=$((t + 6)); done
for k in RIGHT DOWN RIGHT DOWN RIGHT DOWN DOWN DOWN RIGHT; do P="$P;$t:${!k};$((t + 2)):0"; t=$((t + 6)); done
P="$P;$((t + 6)):$CIRCLE;$((t + 8)):0;$((t + 16)):$OPTIONS;$((t + 18)):0;$((t + 24)):$CROSS;$((t + 26)):0"
rc=$(run "$T" "$P" "$t")
expect "[ $rc = 0 ]" "settings screen: exit code 0 (got $rc)"
expect "grep -q '^pad_type=1$' $T/root/pce-ps5.ini && grep -q '^multitap=0$' $T/root/pce-ps5.ini && grep -q '^turbo_mode=1$' $T/root/pce-ps5.ini" "the settings screen set a 6-button pad, the multitap off, the turbo switches"
expect "grep -q '^btn_2=1$' $T/root/pce-ps5.ini" "the settings screen moved II to Circle"
nosan "$T"
fi

if want 21; then
echo "== 21. CRT shaders: CRT Easymode style by default, every shader draws, the choice is kept"
SHADERS=("Off" "CRT Easymode style" "crt-lottes" "crt-lottes-fast" "crt-1tap" "crt-2tap" "crt-hyllian-fast" "crt-nobody" "newpixie-mini" "crt-blurPi-sharp" "crt-blurPi-soft" "monoCRT")
T=$(newroot t21)
rm -f "$T/root/pce-ps5.ini" # a first start: no settings file yet
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 160)" "90,130" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'shader CRT Easymode style' $T/root/logs/boot.log" "a first start draws the game through CRT Easymode style"
expect "python3 - $T/dump/flip00090.ppm <<'PY'
import sys
d = open(sys.argv[1], 'rb').read().split(b'\n', 3)
w, h = map(int, d[1].split()); px = d[3]
R = lambda x, y: px[(y * w + x) * 3]
col = [R(960, y) for y in range(400, 460)] # down the middle: scanlines
row = [R(x, 540) for x in range(900, 960)] # across: the aperture grille
sys.exit(0 if max(col) > 150 and min(col) < 0.85 * max(col) and len(set(row)) > 1 else 1)
PY" "scanlines and a phosphor mask on the red picture"
expect "$CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null || python3 -c \"import sys; d=open('$T/dump/flip00130.ppm','rb').read().split(b'\\n',3); w=int(d[1].split()[0]); p=d[3]; sys.exit(0 if max(p[(y*w+960)*3+1] for y in range(520,560)) > 150 else 1)\"" "Cross -> green through the shader"
expect "grep -q 'shader [0-9.]* ms' $T/root/logs/boot.log" "the shader's drawing time is logged"
nosan "$T"
for s in $(seq 1 11); do
	T=$(newroot t21s$s)
	echo "shader=$s" >>"$T/root/pce-ps5.ini"
	$ROM "$T/root/roms/test.pce" >/dev/null
	rc=$(run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.pce")
	expect "[ $rc = 0 ] && grep -q 'shader ${SHADERS[$s]})' $T/root/logs/boot.log && python3 -c \"import sys; d=open('$T/dump/flip00090.ppm','rb').read().split(b'\\n',3); w=int(d[1].split()[0]); p=d[3]; sys.exit(0 if max(p[(y*w+960)*3] for y in range(500,580)) > 60 else 1)\"" "${SHADERS[$s]} draws the picture"
	expect "! grep -q 'runtime error\|AddressSanitizer' $T/out.txt" "${SHADERS[$s]}: no sanitizer reports"
done
T=$(newroot t21m)
$ROM "$T/root/roms/Alpha (USA).pce" >/dev/null
# Triangle -> settings, the first row is Shader: Right twice -> crt-lottes-fast; Circle -> back; Options + Cross -> quit
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;40:$RIGHT;42:0;46:$RIGHT;48:0;52:$RIGHT;54:0;60:$CIRCLE;62:0;70:$OPTIONS;72:0;80:$CROSS;82:0" "50")
expect "grep -q '^shader=3$' $T/root/pce-ps5.ini" "the settings screen's Shader row is saved (shader=0 -> 3)"
fi

if want 22; then
echo "== 22. the helper: unknown titles refused, a slow client can't block it, wanted.txt must be a plain file"
waitfor() { for i in $(seq 1 50); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 0.1; done; return 1; }
stop_helpers() { pkill -f 'build/host/pce-ps5-(installer|helper)' 2>/dev/null; pkill -f 'received.elf' 2>/dev/null; sleep 0.3; }
stop_helpers
T=$(newroot t22)
$ROM "$T/root/roms/test.pce" >/dev/null
# a process whose title can't be read (here: the host reports none) is not let out
PCEPS5_HOST_JB_TITLE= PCEPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
rc=$(run "$T" "$(QUITAT 30)" "" "$T/root/roms/test.pce")
expect "grep -q 'title unknown: not Beetle PCE PS5' $T/hroot/logs/helper.log && ! grep -q 'letting it out' $T/hroot/logs/helper.log" "a process of unknown title is refused (fail closed)"
stop_helpers
# a client that sends one byte a second doesn't hold the helper: the app's request still goes through
T=$(newroot t22b)
$ROM "$T/root/roms/test.pce" >/dev/null
PCEPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
python3 - "$PCEPS5_HELPER_PORT" <<'PY' &
import socket, sys, time
s = socket.create_connection(('127.0.0.1', int(sys.argv[1])))
try:
    for i in range(20):
        s.send(b'x'); time.sleep(1)
except OSError:
    pass
PY
DRIP=$!
sleep 0.5
start=$(date +%s)
rc=$(run "$T" "$(QUITAT 30)" "" "$T/root/roms/test.pce")
secs=$(( $(date +%s) - start ))
expect "grep -q 'Beetle PCE helper (port [0-9]*): ret 0' $T/root/logs/boot.log" "with a slow client connected, the app is still let out"
expect "[ $secs -lt 15 ]" "and without waiting for the slow client (${secs}s)"
kill $DRIP 2>/dev/null
stop_helpers
# covers/wanted.txt as a symbolic link to another file: not read
T=$(newroot t22c)
mkdir -p "$T/hroot/covers"
echo "secret" >"$T/secret.txt"
ln -s "$T/secret.txt" "$T/hroot/covers/wanted.txt"
PCEPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
python3 - "$PCEPS5_HELPER_PORT" "$T/answer.bin" <<'PY'
import socket, struct, sys
s = socket.create_connection(('127.0.0.1', int(sys.argv[1])))
req = bytearray(0xA10)
struct.pack_into('<IiI', req, 0, 0xDEADBEEF, 6, 1234)
s.sendall(req)
data = b''
while True:
    d = s.recv(65536)
    if not d: break
    data += d
open(sys.argv[2], 'wb').write(data)
PY
expect "grep -q 'wanted-covers list, 0 bytes' $T/hroot/logs/helper.log && ! grep -q secret $T/answer.bin" "a wanted.txt that is a symbolic link is not followed"
stop_helpers
fi

if want 23; then
echo "== 23. saves: one folder per system, the backup RAM written when the game changes it (an erase too), not otherwise"
# a HuCard and a CD of the same name: each its own state
T=$(newroot t23)
mkdir -p "$T/root/bios" && $ROM "$T/root/bios/syscard3.pce" >/dev/null
$ROM "$T/root/roms/test.pce" >/dev/null
head -c $((2352 * 300)) /dev/zero >"$T/root/roms/test.bin"
printf 'FILE "test.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n' >"$T/root/roms/test.cue"
rc=$(run "$T" "0:0;100:$L2UP;105:0;$(QUITAT 140)" "" "$T/root/roms/test.pce")
rc2=$(run "$T" "0:0;100:$L2DOWN;105:0;$(QUITAT 140)" "" "$T/root/roms/test.cue")
expect "[ $rc = 0 ] && [ $rc2 = 0 ]" "exit codes 0 (got $rc, $rc2)"
expect "[ -f $T/root/states/PCEngine/test.state1 ] && [ ! -e $T/root/states/PCEngineCD/test.state1 ]" "the HuCard's state went to states/PCEngine/"
expect "! grep -q 'load state 1' $T/root/logs/boot.log" "the CD game of the same name didn't load it"
nosan "$T"
# the backup RAM: the card writes \$5A; unchanged it isn't written again; erased (II held) the erase is saved
T=$(newroot t23c)
$ROM "$T/root/roms/save.pce" >/dev/null
rc=$(run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/save.pce")
S=$T/root/saves/PCEngine/save.srm
expect "[ -f $S ] && python3 -c 'import sys; d=open(\"$S\",\"rb\").read(); sys.exit(0 if len(d) == 2048 and d[:4] == b\"HUBM\" and d[0x10] == 0x5A else 1)'" "the backup RAM (2 KiB, HUBM) holds what the game wrote"
ino=$(stat -c %i "$S" 2>/dev/null)
python3 -c "d=bytearray(open('$S','rb').read()); d[0x20]=0x77; open('$S','wb').write(d)"
ino=$(stat -c %i "$S" 2>/dev/null)
rc=$(run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/save.pce")
expect "[ \"$(stat -c %i "$S" 2>/dev/null)\" = '$ino' ] && python3 -c 'import sys; sys.exit(0 if open(\"$S\",\"rb\").read()[0x20] == 0x77 else 1)'" "the saved backup RAM is loaded into the game, and not written again unchanged"
rc=$(run "$T" "0:0;20:$CROSS;$(QUITAT 100)" "" "$T/root/roms/save.pce")
expect "[ -f $S ] && python3 -c 'import sys; sys.exit(0 if open(\"$S\",\"rb\").read()[0x10] == 0 else 1)'" "the game's erase was saved"
nosan "$T"
fi

if want 24; then
echo "== 24. edge cases: 720p picture edges, Resume doesn't press II, odd library files, long paths, covers kept, downloads after a tab change"
SQUARE=8000
REPO=NEC_-_PC_Engine_-_TurboGrafx_16
# 720p: the whole picture follows the game, its edges included (the damage is mapped to 1280x720)
T=$(newroot t24)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(PCEPS5_HOST_DIRECT_MAX_MIB=12 run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 200)" "130" "$T/root/roms/test.pce")
expect "grep -q 'scan-out 1280x720' $T/root/logs/boot.log" "720p scan-out"
expect "$CHECK $T/dump/flip00130.ppm 260 360 green >/dev/null && $CHECK $T/dump/flip00130.ppm 1020 360 green >/dev/null" "720p: Cross turns the picture green to its edges"
nosan "$T"
# Resume with Cross: while Cross is still held the game doesn't see II (the picture stays red)
T=$(newroot t24b)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(run "$T" "0:0;100:$L3R3;102:0;120:$CROSS;200:0;$(QUITAT 260)" "190" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "$CHECK $T/dump/flip00190.ppm 960 540 red >/dev/null" "Cross held after Resume doesn't reach the game"
nosan "$T"
# the library: a FIFO doesn't hang the scan; an unquoted cue FILE hides its track; a zip entry with a 600-character name
T=$(newroot t24c)
mkdir -p "$T/root/roms/PCEngine" "$T/root/roms/PCEngineCD/Disc"
mkfifo "$T/root/roms/PCEngine/pipe.pce"
$ROM "$T/root/roms/PCEngine/a.pce" >/dev/null
$ROM "$T/root/roms/PCEngine/b.pce" header >/dev/null
printf 'FILE track01.cue BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' >"$T/root/roms/PCEngineCD/Disc/Disc.cue"
printf 'not a sheet' >"$T/root/roms/PCEngineCD/Disc/track01.cue"
python3 - "$T/root/roms/PCEngine/long.zip" "$T/root/roms/PCEngine/a.pce" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.writestr('x' * 600 + '.pce', b'\0' * 1024)
    z.write(sys.argv[2], 'inner.pce')
PY
rc=$(MAXFLIPS=400 run "$T" "0:0;30:$OPTIONS;32:0;40:$CROSS;42:0" "")
expect "[ $rc = 0 ]" "the shelf came up and quit (got $rc)"
expect "! grep -q 'pipe.pce' $T/root/logs/boot.log" "a FIFO is not listed (nor read)"
expect "! grep -q 'track01.cue \[' $T/root/logs/boot.log && grep -q 'Disc.cue \[' $T/root/logs/boot.log" "FILE track01.cue (no quotes): the named file is the disc's, not a game"
expect "grep -q 'long.zip \[pce\]' $T/root/logs/boot.log" "a zip with a 600-character entry name still lists its game"
expect "grep -q 'PCEngine/b.pce' $T/root/covers/crc-cache.txt" "the CRC cache holds b.pce"
rm -f "$T/root/roms/PCEngine/b.pce"
rc=$(MAXFLIPS=400 run "$T" "0:0;30:$OPTIONS;32:0;40:$CROSS;42:0" "")
expect "! grep -q 'PCEngine/b.pce' $T/root/covers/crc-cache.txt" "a deleted ROM leaves the CRC cache"
nosan "$T"
# a path of 1024 characters or more (the console's PATH_MAX): refused with a message
T=$(newroot t24d)
D="$T/root/roms"; for i in 1 2 3 4 5; do D="$D/$(printf 'd%.0s' $(seq 1 200))"; done
mkdir -p "$D"
$ROM "$D/test.pce" >/dev/null
rc=$(MAXFLIPS=300 run "$T" "0:0;100:$CROSS;102:0" "" "$D/test.pce")
expect "grep -q 'the path is [0-9]* characters long' $T/root/logs/boot.log" "a too-long path is refused"
nosan "$T"
# a card bigger than the biggest mapper (8.5 MiB): the core refuses it, nothing overruns
T=$(newroot t24h)
$ROM "$T/root/roms/huge.pce" big:9216 >/dev/null
rc=$(MAXFLIPS=300 run "$T" "0:0;100:$CROSS;102:0" "" "$T/root/roms/huge.pce")
expect "grep -q 'could not start .*huge.pce' $T/root/logs/boot.log" "a 9 MiB card is refused"
nosan "$T"
# Square (fetch the cover again) while offline: the cover it has is kept
T=$(newroot t24e)
mkdir -p "$T/root/roms/PCEngine" "$T/root/covers/PCEngine"
$ROM "$T/root/roms/PCEngine/R-Type (USA).pce" >/dev/null
python3 -c "from PIL import Image; Image.new('RGB',(512,357),(255,0,0)).save('$T/root/covers/PCEngine/R-Type (USA).png')"
rc=$(run "$T" "0:0;40:$SQUARE;42:0;80:$OPTIONS;82:0;90:$CROSS;92:0" "")
expect "[ -s '$T/root/covers/PCEngine/R-Type (USA).png' ]" "offline, Square doesn't delete the cover"
nosan "$T"
# online, Square on a ROM named as its official name: the server has none (404) -> the cover stays; the server has
# it -> the new one replaces it
cp "$T/root/covers/PCEngine/R-Type (USA).png" "$T/old.png"
SRV=$T/srv/$REPO/Named_Boxarts; mkdir -p "$SRV"
PORT=18094
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png"
SQ="0:0;40:$SQUARE;42:0;200:$OPTIONS;202:0;210:$CROSS;212:0"
rc=$(OFFLINE= COVER_URL="$URL" PCEPS5_HOST_REALTIME=1 run "$T" "$SQ" "")
expect "cmp -s '$T/root/covers/PCEngine/R-Type (USA).png' $T/old.png && [ ! -e '$T/root/covers/PCEngine/R-Type (USA).refetch' ]" "the server has none (404): the cover stays"
python3 -c "from PIL import Image; Image.new('RGB',(512,357),(0,255,0)).save('$SRV/R-Type (USA).png')"
rm -f "$T/root/covers/PCEngine/"*.missing
rc=$(OFFLINE= COVER_URL="$URL" PCEPS5_HOST_REALTIME=1 run "$T" "$SQ" "")
kill $SRVPID 2>/dev/null
expect "cmp -s '$T/root/covers/PCEngine/R-Type (USA).png' '$SRV/R-Type (USA).png'" "the server has it: the new cover replaces the old one"
nosan "$T"
# as on the console (a helper answers, so the shelf downloads nothing): Square puts the cover on the wanted list
# and the helper fetches it in the background; the old cover stays until the new one lands
T2=$(newroot t24g)
mkdir -p "$T2/root/roms/PCEngine" "$T2/root/covers/PCEngine"
$ROM "$T2/root/roms/PCEngine/R-Type (USA).pce" >/dev/null
cp "$T/old.png" "$T2/root/covers/PCEngine/R-Type (USA).png"
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
PCEPS5_ROOT=$T2/root ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T2/helper.txt" 2>&1 &
for i in $(seq 1 50); do grep -q listening "$T2/root/logs/helper.log" 2>/dev/null && break; sleep 0.1; done
rc=$(OFFLINE= COVER_URL="$URL" PCEPS5_HOST_REALTIME=1 run "$T2" "0:0;40:$SQUARE;42:0;300:$OPTIONS;302:0;310:$CROSS;312:0" "")
expect "grep -q 'PCEngine/R-Type (USA).png' $T2/root/covers/wanted.txt && ! grep -q 'restarting' $T2/root/logs/boot*.log" "console: Square lists the cover for the helper, no restart"
expect "grep -q 'PCEngine/R-Type (USA).png -> 200' $T2/root/logs/helper.log && cmp -s '$T2/root/covers/PCEngine/R-Type (USA).png' '$SRV/R-Type (USA).png' && [ ! -e '$T2/root/covers/PCEngine/R-Type (USA).refetch' ]" "the helper fetched the new cover over the old one"
kill $SRVPID 2>/dev/null
pkill -f "$HELPER" 2>/dev/null; sleep 0.3
# downloads still work after the shelf restarted its cover worker (another tab)
T=$(newroot t24f)
SRV=$T/srv/$REPO/Named_Boxarts; mkdir -p "$SRV" "$T/root/roms/PCEngine" "$T/root/roms/SuperGrafx" "$T/root/covers/SuperGrafx"
python3 -c "from PIL import Image; Image.new('RGB',(512,357),(255,0,0)).save('$SRV/R-Type (USA).png'); Image.new('RGB',(360,512),(0,0,255)).save('$T/root/covers/SuperGrafx/Battle Ace (Japan) (En).png')"
$ROM "$T/root/roms/PCEngine/R-Type (USA).pce" >/dev/null
$ROM "$T/root/roms/SuperGrafx/Battle Ace (Japan) (En).sgx" >/dev/null
echo "shelf_family=2" >>"$T/root/pce-ps5.ini"
PORT=18082
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
rc=$(OFFLINE= COVER_URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png" PCEPS5_HOST_REALTIME=1 run "$T" \
	"0:0;60:$DOWN;62:0;300:$OPTIONS;302:0;310:$CROSS;312:0" "")
kill $SRVPID 2>/dev/null
expect "grep -q 'tab All games' $T/root/logs/boot.log" "moved to another tab (the cover worker restarted)"
expect "cmp -s '$T/root/covers/PCEngine/R-Type (USA).png' '$SRV/R-Type (USA).png'" "the new tab's cover was downloaded"
nosan "$T"
fi

if want 25; then
echo "== 25. debug logs off: nothing written (app and helper), earlier logs kept; the setting switches them at once"
SHELFQUIT_AT() { echo "$1:$OPTIONS;$(($1 + 2)):0;$(($1 + 10)):$CROSS;$(($1 + 12)):0"; }
# off in the ini: the game runs, no line anywhere, the previous run's boot.log untouched
T=$(newroot t25)
$ROM "$T/root/roms/test.pce" >/dev/null
echo "debug_logs=0" >>"$T/root/pce-ps5.ini"
mkdir -p "$T/root/logs" && echo "OLD RUN" >"$T/root/logs/boot.log"
rc=$(run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.pce")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null" "the game runs"
expect "[ \"\$(cat $T/root/logs/boot.log)\" = 'OLD RUN' ] && [ ! -e $T/root/logs/boot.prev.log ]" "the earlier boot.log is kept as it was, nothing new written"
expect "! grep -q '^\[pce-ps5' $T/out.txt" "nothing on stdout either"
expect "grep -q '^debug_logs=0' $T/root/pce-ps5.ini" "the setting stays off"
nosan "$T"
# the default (1.0.1): no debug_logs key and no test override -> nothing written, and the settings file says 0
T=$(newroot t25z)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(PCEPS5_LOGS_DEFAULT= run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.pce")
expect "[ $rc = 0 ] && [ ! -e $T/root/logs/boot.log ] && ! grep -q '^\[pce-ps5' $T/out.txt" "debug logs off by default: no boot.log"
expect "grep -q '^debug_logs=0' $T/root/pce-ps5.ini" "the default settings file has debug_logs=0"
nosan "$T"
# Settings (Triangle) -> Debug logs (Up three times from Shader: Back, Updates, then Debug logs) -> Off: that line is the last one
T=$(newroot t25b)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;50:$UP;52:0;60:$UP;62:0;65:$UP;67:0;70:$CROSS;72:0;90:$CIRCLE;92:0;$(SHELFQUIT_AT 120)" "")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '^debug_logs=0' $T/root/pce-ps5.ini" "Debug logs Off saved"
expect "tail -1 $T/root/logs/boot.log | grep -q 'debug logs turned off'" "the last line of boot.log says the logs were turned off"
nosan "$T"
# and back on: logging starts again in the same boot.log
T=$(newroot t25c)
$ROM "$T/root/roms/test.pce" >/dev/null
echo "debug_logs=0" >>"$T/root/pce-ps5.ini"
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;50:$UP;52:0;60:$UP;62:0;65:$UP;67:0;70:$CROSS;72:0;90:$CIRCLE;92:0;$(SHELFQUIT_AT 120)" "")
expect "grep -q '^debug_logs=1' $T/root/pce-ps5.ini" "Debug logs On saved"
expect "grep -q 'debug logs turned on' $T/root/logs/boot.log && ! grep -q 'Beetle PCE PS5 1\.[0-9.]*, Beetle PCE' $T/root/logs/boot.log" "boot.log starts at the switch (nothing from before it)"
nosan "$T"
# the helper follows the setting too
stop_helpers25() { pkill -f 'build/host/pce-ps5-(installer|helper)' 2>/dev/null; sleep 0.3; }
stop_helpers25
T=$(newroot t25d)
$ROM "$T/root/roms/test.pce" >/dev/null
mkdir -p "$T/hroot" && echo "debug_logs=0" >"$T/hroot/pce-ps5.ini"
PCEPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
sleep 1.5
rc=$(run "$T" "$(QUITAT 30)" "" "$T/root/roms/test.pce")
expect "grep -q 'Beetle PCE helper (port [0-9]*): ret 0' $T/root/logs/boot.log" "the helper still lets the app out"
expect "[ ! -e $T/hroot/logs/helper.log ] && ! grep -q 'helper\]' $T/helper.txt" "with debug logs off the helper writes no log"
stop_helpers25
fi

if want 26; then
echo "== 26. the helper's own HTTPS (Mbed TLS): certificate checked, kept connection, chunks, redirects, 404"
stop_helpers26() { pkill -f 'build/host/pce-ps5-(installer|helper)' 2>/dev/null; sleep 0.3; }
stop_helpers26
C=$WORK/t26-certs; rm -rf "$C"; mkdir -p "$C"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$C/ca.key" -out "$C/ca.pem" -days 30 -subj "/CN=Genplus Test CA" 2>/dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$C/srv.key" -out "$C/srv.csr" -subj "/CN=localhost" 2>/dev/null
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n' >"$C/ext.cnf"
openssl x509 -req -in "$C/srv.csr" -CA "$C/ca.pem" -CAkey "$C/ca.key" -CAcreateserial -out "$C/srv.pem" -days 30 -extfile "$C/ext.cnf" 2>/dev/null
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$C/other.key" -out "$C/other.pem" -days 30 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" 2>/dev/null
SRVROOT=$WORK/t26-srv; rm -rf "$SRVROOT"; mkdir -p "$SRVROOT/Repo/Named_Boxarts"
python3 -c "
from PIL import Image
for n,c in [('A b',(255,0,0)),('B',(0,255,0)),('C',(0,0,255))]: Image.new('RGB',(512,357),c).save('$SRVROOT/Repo/Named_Boxarts/'+n+'.png')"
PORT=18443
tls_case() { # name cert key mode host -> $T with the helper's run in it
	T=$(newroot "t26-$1")
	mkdir -p "$T/root/covers"
	python3 tests/https_server.py "$SRVROOT" $PORT "$2" "$3" "$4" 2>"$T/server.txt" &
	local sp=$!
	sleep 0.6
	local u="https://$5:$PORT"
	printf 'PCEngine/A.png\t%s/Repo/Named_Boxarts/A%%20b.png\nPCEngine/B.png\t%s/chunked/Repo/Named_Boxarts/B.png\nPCEngine/C.png\t%s/moved/Repo/Named_Boxarts/C.png\nPCEngine/D.png\t%s/Repo/Named_Boxarts/D.png\n' "$u" "$u" "$u" "$u" >"$T/root/covers/wanted.txt"
	PCEPS5_EXTRA_CA=$C/ca.pem PCEPS5_ROOT=$T/root ASAN_OPTIONS=detect_leaks=0 timeout ${6:-6} "$HELPER" >"$T/helper.txt" 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	stop_helpers26
}
tls_case good "$C/srv.pem" "$C/srv.key" keep localhost
H="$T/root/logs/helper.log"; D="$T/root/covers/PCEngine"; S="$SRVROOT/Repo/Named_Boxarts"
expect "grep -q '\[https\] Mbed TLS 3\.6\.[0-9]*: [0-9]* CA certificate(s), + the extra CA' $H" "Mbed TLS set up with Mozilla's CA list (+ the test CA)"
expect "cmp -s '$D/A.png' '$S/A b.png'" "a cover over HTTPS (Content-Length)"
expect "cmp -s '$D/B.png' '$S/B.png'" "a chunked answer"
expect "cmp -s '$D/C.png' '$S/C.png' && grep -q 'GET /moved/' $T/server.txt" "a redirect followed"
expect "[ -f '$D/D.missing' ] && [ ! -f '$D/D.png' ]" "a 404: marked missing"
expect "[ \$(grep -c 'kept connection' $H) = 3 ]" "one connection for the four requests"
expect "! grep -q '\[http\] ' $H" "the helper doesn't use libSceHttp2"
expect "! grep -q 'runtime error\|AddressSanitizer' $T/helper.txt" "no sanitizer reports"
tls_case untrusted "$C/other.pem" "$C/other.key" keep localhost 4
H="$T/root/logs/helper.log"; D="$T/root/covers/PCEngine"
expect "grep -q 'Certificate verification failed' $H && [ -z \"\$(ls $D 2>/dev/null)\" ]" "a certificate from an unknown CA: refused, nothing saved, nothing marked missing"
tls_case wrongname "$C/srv.pem" "$C/srv.key" keep 127.0.0.1 4
H="$T/root/logs/helper.log"; D="$T/root/covers/PCEngine"
expect "grep -q 'Certificate verification failed' $H && [ -z \"\$(ls $D 2>/dev/null)\" ]" "a certificate for another name: refused"
tls_case closing "$C/srv.pem" "$C/srv.key" drop localhost
D="$T/root/covers/PCEngine"
expect "[ -f '$D/A.png' ] && [ -f '$D/B.png' ] && [ -f '$D/C.png' ]" "a server that closes kept connections: a new one each time"
fi

if want 27; then
echo "== 27. the installer keeps to its own title: our other ports' folders (PPSA99921/22/24) untouched"
stop27() { pkill -f 'build/host/pce-ps5-(installer|helper)' 2>/dev/null; sleep 0.3; }
waitfor27() { for i in $(seq 1 80); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 0.1; done; return 1; }
stop27
T=$(newroot t27)
for id in PPSA99921 PPSA99922 PPSA99924; do mkdir -p "$T/homebrew/$id/sce_sys"; echo "$id" >"$T/homebrew/$id/eboot.bin"; echo "{\"titleId\": \"$id\"}" >"$T/homebrew/$id/sce_sys/param.json"; done
before=$(cd "$T/homebrew" && find . -type f -exec md5sum {} + | sort)
PCEPS5_ROOT=$T/root PCEPS5_HOMEBREW=$T/homebrew PCEPS5_APPMETA=$T/appmeta ASAN_OPTIONS=detect_leaks=0 timeout 60 "$INSTALLER" >"$T/inst.txt" 2>&1 &
expect "waitfor27 $T/root/logs/installer.log 'listening on 127.0.0.1'" "installed, then stays as the helper"
expect "cmp -s $T/homebrew/PPSA99925/eboot.bin tests/fake-eboot.bin && grep -q '\"titleId\": \"PPSA99925\"' $T/homebrew/PPSA99925/sce_sys/param.json" "the app installed as PPSA99925"
after=$(cd "$T/homebrew" && find . -type f -not -path './PPSA99925/*' -exec md5sum {} + | sort)
expect "[ \"\$before\" = \"\$after\" ] && [ ! -e $T/appmeta/PPSA99921 ]" "the other ports' folders and art: untouched"
expect "! grep -q 'runtime error\|AddressSanitizer' $T/inst.txt" "no sanitizer reports"
stop27
fi

if want 28; then
echo "== 28. updates from GitHub's releases (Settings, SYSTEM, Updates): the helper checks, downloads, checks every file, installs"
T=$(newroot t28)
UPD="python3 tests/update_client.py $PCEPS5_HELPER_PORT"
stop28() { pkill -f 'build/host/pce-ps5-(installer|helper)' 2>/dev/null; pkill -f 'http.server 1[89][0-9][0-9][0-9]' 2>/dev/null; sleep 0.3; }
waitfor28() { for i in $(seq 1 80); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 0.1; done; return 1; }
stop28
SRV=$T/srv; mkdir -p "$SRV" "$T/appmeta/PPSA99925"; echo old >"$T/appmeta/PPSA99925/icon0.png"
SPORT=$((18000 + RANDOM % 1900))
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
# the release: the installed app with a new eboot, a newer param.json, a new file; the old manifest lists a file that goes
release() { # tag contentVersion [bad]
	rm -rf "$T/rel" && mkdir -p "$T/rel" && cp -r "$T/homebrew/PPSA99925" "$T/rel/PPSA99925" && rm -f "$T/rel/PPSA99925/manifest.sha256" "$T/rel/PPSA99925/sce_sys/old.bin"
	echo "eboot $1" >"$T/rel/PPSA99925/eboot.bin"; echo "new art" >"$T/rel/PPSA99925/sce_sys/icon0.png"; echo extra >"$T/rel/PPSA99925/sce_sys/extra.bin"
	sed -i "s/\"contentVersion\": \"[0-9.]*\"/\"contentVersion\": \"$2\"/" "$T/rel/PPSA99925/sce_sys/param.json"
	python3 tools/make_release_zip.py "$T/rel/PPSA99925" PPSA99925 "$SRV/BeetlePCEPS5-$1-PPSA99925.zip" >/dev/null
	local sha size
	sha=$(sha256sum "$SRV/BeetlePCEPS5-$1-PPSA99925.zip" | cut -c1-64); size=$(stat -c %s "$SRV/BeetlePCEPS5-$1-PPSA99925.zip")
	[ -n "${3:-}" ] && sha=0000${sha:4}
	printf '{"tag_name":"%s","name":"Beetle PCE PS5 %s","assets":[{"name":"BeetlePCEPS5-%s.elf","size":1,"browser_download_url":"http://127.0.0.1:%s/x.elf"},{"name":"BeetlePCEPS5-%s-PPSA99925.zip","size":%s,"digest":"sha256:%s","browser_download_url":"http://127.0.0.1:%s/BeetlePCEPS5-%s-PPSA99925.zip"}]}' \
		"$1" "$1" "$1" $SPORT "$1" $size $sha $SPORT "$1" >"$SRV/latest.json"
}
# the app installed by the payload (1.0), which then stays as the helper; the updater asks the test server
PCEPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json PCEPS5_ROOT=$T/root PCEPS5_HOMEBREW=$T/homebrew PCEPS5_APPMETA=$T/appmeta \
	ASAN_OPTIONS=detect_leaks=0 timeout 120 "$INSTALLER" >"$T/inst.txt" 2>&1 &
expect "waitfor28 $T/root/logs/installer.log 'listening on 127.0.0.1'" "the payload installed 1.0 and serves as the helper"
expect "[ \"\$($UPD status 1.0 | cut -d'|' -f1)\" = idle ]" "the updater answers: idle"
release v1.0 01.000.000
expect "$UPD check 1.0 --until uptodate,failed | grep -q '^uptodate|1.0|'" "the newest release is this version: up to date"
echo gone >"$T/homebrew/PPSA99925/sce_sys/old.bin"
printf '%s *sce_sys/old.bin\n' "$(sha256sum <"$T/homebrew/PPSA99925/sce_sys/old.bin" | cut -c1-64)" >"$T/homebrew/PPSA99925/manifest.sha256"
release v9.9 99.000.000 bad
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|'" "a newer release (9.9): available, with its size"
cp "$T/homebrew/PPSA99925/eboot.bin" "$T/eboot.before"
expect "$UPD install 1.0 --until done,failed | grep -q \"^failed|.*SHA-256 doesn't match\"" "a download whose SHA-256 isn't GitHub's: refused"
expect "cmp -s $T/homebrew/PPSA99925/eboot.bin $T/eboot.before && [ -z \"\$(find $T/homebrew -name '*.new')\" ]" "and nothing was changed"
release v9.9 99.000.000
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|'" "checked again"
expect "$UPD install 1.0 --until done,failed | grep -q '^done|9.9|'" "downloaded, every file checked, installed"
expect "grep -q 'eboot v9.9' $T/homebrew/PPSA99925/eboot.bin && grep -q '\"contentVersion\": \"99.000.000\"' $T/homebrew/PPSA99925/sce_sys/param.json" "the new eboot.bin and param.json in place"
expect "[ -f $T/homebrew/PPSA99925/sce_sys/extra.bin ] && [ ! -e $T/homebrew/PPSA99925/sce_sys/old.bin ] && grep -q 'sce_sys/extra.bin' $T/homebrew/PPSA99925/manifest.sha256" "a new file added, one the old manifest had removed, the manifest kept"
expect "grep -q 'new art' $T/appmeta/PPSA99925/icon0.png && [ -z \"\$(find $T/homebrew -name '*.new')\" ]" "the home screen's copy of the art refreshed; no .new files left"
expect "$UPD status 9.9 | grep -q '^done|'" "the result stays until the app is opened again"
stop28
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
# the old payload sent again (an autoload): the newer version is left alone
PCEPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json PCEPS5_ROOT=$T/root PCEPS5_HOMEBREW=$T/homebrew PCEPS5_APPMETA=$T/appmeta \
	ASAN_OPTIONS=detect_leaks=0 timeout 120 "$INSTALLER" >"$T/inst2.txt" 2>&1 &
expect "waitfor28 $T/inst2.txt 'a newer version is installed'" "an older payload sent again: says a newer version is installed"
expect "grep -q 'eboot v9.9' $T/homebrew/PPSA99925/eboot.bin && grep -q 'new art' $T/appmeta/PPSA99925/icon0.png" "and leaves the update (and its art) as they are"
expect "$UPD check 9.9 --until uptodate,available,failed | grep -q '^uptodate|9.9|'" "the app says which version it is: an older helper finds it up to date"
stop28
# the app: at start the daily check (the helper finds 9.9); Settings (Triangle), Up twice to "Updates" (it shows
# "Install 9.9"), Cross: the helper installs it
T2=$(newroot t28b)
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
PCEPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json PCEPS5_ROOT=$T2/root PCEPS5_HOMEBREW=$T2/homebrew PCEPS5_APPMETA=$T2/appmeta \
	ASAN_OPTIONS=detect_leaks=0 timeout 120 "$INSTALLER" >"$T2/inst.txt" 2>&1 &
waitfor28 $T2/root/logs/installer.log 'listening on 127.0.0.1'
$ROM "$T2/root/roms/test.pce" >/dev/null
rc=$(PCEPS5_HOST_REALTIME=1 PCEPS5_APPMETA=$T2/appmeta run "$T2" "0:0;30:$TRIANGLE;32:0;40:$UP;42:0;46:$UP;48:0;56:$CROSS;58:0;150:$CIRCLE;152:0;$(QUITAT 170)" "54,140")
expect "grep -q 'daily check asked of the helper' $T2/root/logs/boot.log && [ -f $T2/root/update-check.txt ]" "at start: the daily check asked of the helper"
expect "grep -q 'update install' $T2/root/logs/installer.log && grep -q 'eboot v9.9' $T2/homebrew/PPSA99925/eboot.bin" "Cross on \"Install 9.9\": the helper installs it"
expect "grep -q 'is available: Settings, SYSTEM, Updates' $T2/inst.txt $T2/root/logs/installer.log" "the daily check found 9.9: a notification says so"
nosan "$T2"
stop28
fi

if want 29; then
echo "== 29. a big library: the scan stays quick (a zip's CRC from its directory, each zip's system remembered)"
T=$(newroot t29)
mkdir -p "$T/root/roms/pce"
$ROM "$T/one.pce" >/dev/null
python3 - "$T/one.pce" "$T/root/roms/pce" <<'PY'
import os, random, sys, zipfile
rom = open(sys.argv[1], "rb").read()
random.seed(3)
words = "Bonk Adventure Blazing Lazers Splatterhouse Bomberman Gunhed Soldier Blade Ninja Spirit Dungeon Explorer Neutopia".split()
for i in range(800):
    name = " ".join(random.sample(words, 3)) + " %04d (Hack)" % i
    with zipfile.ZipFile(os.path.join(sys.argv[2], name + ".zip"), "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(name + ".pce", rom + bytes([i & 255, i >> 8]) * 512)
PY
scan_time() { python3 - "$1" <<'PY'
import re, sys
t = {}
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'\[\s*([0-9.]+)\] \[(boot|games)\] (Beetle PCE PS5 |800 ROM)', line)
    if m: t.setdefault(m.group(2), float(m.group(1)))
print('%.2f' % (t['games'] - t['boot']) if 'boot' in t and 'games' in t else 'none')
PY
}
rc=$(run "$T" "0:0;100:$OPTIONS;102:0;110:$CROSS;112:0" "")
first=$(scan_time $T/root/logs/boot.log)
expect "[ $rc = 0 ] && grep -q '\[games\] 800 ROM(s)' $T/root/logs/boot.log" "800 zipped ROMs found"
expect "[ \$(grep -c '^sys:' $T/root/covers/crc-cache.txt) = 800 ] && [ \$(grep -c '\.zip	[0-9]*	[0-9]*	[0-9A-F]\{8\}	' $T/root/covers/crc-cache.txt) = 800 ]" "each zip's system and CRC remembered"
rc=$(run "$T" "0:0;100:$OPTIONS;102:0;110:$CROSS;112:0" "")
second=$(scan_time $T/root/logs/boot.log)
expect "python3 -c 'import sys; sys.exit(0 if float(\"$first\") < 4 and float(\"$second\") < 2 else 1)'" "first scan ${first} s (under 4), the next ${second} s (under 2)"
nosan "$T"
fi

if want 30; then
echo "== 30. languages: English by default; Spanish, Portuguese (Brazil), French and German translated"
expect "python3 $PWD/tests/check_i18n.py $PWD/frontend/fe_i18n.cpp >/dev/null" "every translation has its five languages and the English format specifiers"
for L in 1 2 3 4; do
	T=$(newroot t30_$L)
	echo "language=$L" >>"$T/root/pce-ps5.ini"
	$ROM "$T/root/roms/Alpha (USA).pce" >/dev/null
	# the settings screen scrolled through, then a game's pause menu
	pad="0:0;20:$TRIANGLE;22:0"; f=30
	for i in $(seq 1 50); do pad="$pad;$f:$DOWN;$((f + 2)):0"; f=$((f + 6)); done
	pad="$pad;$((f + 4)):$CIRCLE;$((f + 6)):0;$((f + 20)):$CROSS;$((f + 22)):0"; f=$((f + 120))
	pad="$pad;$f:$L3R3;$((f + 2)):0"; f=$((f + 10))
	for i in $(seq 1 9); do pad="$pad;$f:$DOWN;$((f + 2)):0"; f=$((f + 8)); done
	pad="$pad;$((f + 10)):$UP;$((f + 12)):0;$((f + 20)):$CROSS;$((f + 22)):0"
	rm -f $T/missing.txt
	rc=$(PCEPS5_HOST_I18N_MISSING=$T/missing.txt run "$T" "$pad" "")
	# what was shown untranslated: only names (the game's, the buttons', the languages') and texts already translated
	python3 - $T/missing.txt $PWD/frontend/fe_i18n.cpp > $T/untranslated.txt <<'PY'
import os, re, sys
src = open(sys.argv[2], encoding='utf-8').read()
translated = set(re.findall(r'"((?:[^"\\]|\\.)*)"', src))
spec = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXeEfgGcsp])')
# a text printed from a translated format: the format as a pattern
formats = [re.compile('^' + ''.join('.*' if spec.fullmatch(part) else re.escape(part)
                                    for part in re.split('(' + spec.pattern + ')', f)) + '$')
           for f in translated if '%' in f]
names = {'Alpha (USA)', 'I', 'II', 'Select', 'Run', 'L1', 'R1', 'OPTIONS', 'English', 'Español', 'Português (Brasil)',
         'Français', 'Deutsch', 'RGB', 'PC Engine', 'SuperGrafx', 'PC Engine CD', 'HuC6280 (PC Engine)'}
for line in (open(sys.argv[1], encoding='utf-8') if os.path.exists(sys.argv[1]) else []):
    t = line.rstrip('\n')
    if t in translated or t in names or not re.search('[A-Za-z]{2}', t) or re.fullmatch(r'[\d%. ]+', t):
        continue
    if any(f.match(t) for f in formats):
        continue
    # the shelf's composed lines: the tab ("↣  " + a translated family name), the game's system/format line
    if t.startswith('↣') or re.match(r'(PC Engine|PC Engine CD|SuperGrafx) {3}', t):
        continue
    if t.startswith('System Card ') or t.startswith('crt-'):
        continue
    print(t)
PY
	expect "[ $rc = 0 ] && [ -s $T/missing.txt ] && [ ! -s $T/untranslated.txt ]" "language $L: every menu text shown is translated ($(head -c 200 $T/untranslated.txt | tr '\n' '|'))"
	expect "grep -q '^language=$L$' $T/root/pce-ps5.ini" "language $L: kept in the settings file"
	nosan "$T"
done
# the network screens (1.1): Settings, Network shares with a share that does not connect, Add a share, the keyboard on
# Server (Circle closes it), the share type switched to NFS, Circle asks to discard (Up, Cross: discard), Connect again;
# back to the shelf: R1 to its Network tab (the share's state), Square scans again, L1 back, quit
for L in 1 2 3 4; do
	T=$(newroot t30net_$L)
	echo "language=$L" >>"$T/root/pce-ps5.ini"
	echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
	$ROM "$T/root/roms/Alpha (USA).pce" >/dev/null
	key() { pad="$pad;$f:$1;$((f + 2)):0"; f=$((f + ${2:-6})); } # one press, then the next one $2 flips later
	pad="0:0;20:$TRIANGLE;22:0"; f=30
	for i in $(seq 1 5); do key $UP; done
	key $CROSS 20; key $DOWN; key $CROSS 20; key $CROSS 20; key $CIRCLE 20
	key $UP; key $RIGHT 20; key $CIRCLE 20; key $UP; key $CROSS 20
	key $DOWN; key $CROSS 20; key $CIRCLE 20; key $CIRCLE 30
	key $R1 30; key $SQUARE 60; key $L1 30
	key $OPTIONS 10; key $CROSS 10
	rm -f $T/missing.txt
	rc=$(PCEPS5_HOST_REALTIME=1 PCEPS5_HOST_NETFS_DIR=$T/nothing PCEPS5_HOST_I18N_MISSING=$T/missing.txt run "$T" "$pad" "")
	python3 - $T/missing.txt $PWD/frontend/fe_i18n.cpp > $T/untranslated.txt <<'PY'
import os, re, sys
src = open(sys.argv[2], encoding='utf-8').read()
translated = set(re.findall(r'"((?:[^"\\]|\\.)*)"', src))
spec = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXeEfgGcsp])')
formats = [re.compile('^' + ''.join('.*' if spec.fullmatch(part) else re.escape(part)
                                    for part in re.split('(' + spec.pattern + ')', f)) + '$')
           for f in translated if '%' in f]
# (the first walk's names and rules, and the network screens' names)
names = {'Alpha (USA)', 'I', 'II', 'Select', 'Run', 'L1', 'R1', 'OPTIONS', 'English', 'Español', 'Português (Brasil)',
         'Français', 'Deutsch', 'RGB', 'PC Engine', 'SuperGrafx', 'PC Engine CD', 'HuC6280 (PC Engine)',
         'SMB (Windows, Samba, NAS)', 'NFS', 'USB'}
for line in (open(sys.argv[1], encoding='utf-8') if os.path.exists(sys.argv[1]) else []):
    t = line.rstrip('\n')
    if t in translated or t in names or not re.search('[A-Za-z]{2}', t) or re.fullmatch(r'[\d%. ]+', t):
        continue
    if any(f.match(t) for f in formats):
        continue
    if t.startswith('↣') or re.match(r'(PC Engine|PC Engine CD|SuperGrafx) {3}', t):
        continue
    if t.startswith('System Card ') or t.startswith('crt-'):
        continue
    print(t)
PY
	expect "[ $rc = 0 ] && grep -q 'network tab: scan again' $T/root/logs/boot.log && [ ! -s $T/untranslated.txt ]" "language $L: the network screens and the Network tab are translated ($(head -c 200 $T/untranslated.txt | tr '\n' '|'))"
	nosan "$T"
done
# English stays the default: no setting, English on the screen (the texts pass through as they are)
T=$(newroot t30en)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(PCEPS5_HOST_I18N_MISSING=$T/missing.txt run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/test.pce")
expect "[ $rc = 0 ] && [ ! -s $T/missing.txt ] && grep -q '^language=0$' $T/root/pce-ps5.ini" "English by default (language=0): nothing is looked up"
nosan "$T"
# the Language row (SYSTEM): Settings, Up four times from Shader (Back, Updates, Debug logs, Language), Right -> Español
T=$(newroot t30row)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;50:$UP;52:0;56:$UP;58:0;62:$UP;64:0;68:$UP;70:0;76:$RIGHT;78:0;90:$CIRCLE;92:0;100:$OPTIONS;102:0;110:$CROSS;112:0" "")
expect "[ $rc = 0 ] && grep -q '^language=1$' $T/root/pce-ps5.ini" "Settings, SYSTEM, Language: Right picks Español, saved as language=1"
nosan "$T"
fi

# ---- network shares (1.1): netfs, games on a share, the Network shares screen, the shelf's storage tabs -------------
# From Genesis Plus GX PS5 2.1.1's groups 39-43 (thatboialex's design in Snes9x PS5). The host build's share backend
# (host/netfs_dirbackend.cpp) answers every smb:// and nfs:// address with the folder PCEPS5_HOST_NETFS_DIR names.

if want 31; then
echo "== 31. netfs: share addresses and redaction, routing, the libc --wrap layer, minizip and zlib on a share, the watchdog (in-memory share)"
NETFS_OUT=$(ASAN_OPTIONS=detect_leaks=1 timeout 120 "$PWD/build/host/test-netfs" 2>&1)
NETFS_RC=$?
echo "$NETFS_OUT" | tail -3 | sed 's/^/  /'
expect "[ $NETFS_RC = 0 ]" "netfs: every check passes"
expect "! echo \"\$NETFS_OUT\" | grep -q 'runtime error\|Sanitizer'" "no sanitizer reports"
fi

if want 32; then
echo "== 32. games on a share: on the shelf, a HuCard started from it, CD games (cue/bin, chd) whose music streams from it while they run"
# a. a HuCard on an SMB share: listed, started from the shelf (read whole), its save state on the console
T=$(newroot t32)
mkdir -p "$T/share/PCEngine"
$ROM "$T/share/PCEngine/Net Game (USA).pce" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_REALTIME=1 MAXFLIPS=900 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0;280:$CROSS;320:0;340:$L2UP;345:0;$(QUITAT 400)" "300")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '\[net\] 1 share(s) in shares.txt' $T/root/logs/boot.log" "shares.txt was read"
expect "grep -q '\[net\] Connected to smb://10.0.0.5/games' $T/root/logs/boot.log" "the share connected"
expect "grep -q 'loading /smb/10.0.0.5/games/PCEngine/Net Game (USA).pce' $T/root/logs/boot.log && grep -q 'running \"Net Game (USA)\" (PC Engine)' $T/root/logs/boot.log" "the game was started from the share"
expect "$CHECK $T/dump/flip00300.ppm 960 540 green >/dev/null" "it runs (Cross -> green)"
expect "[ -f '$T/root/states/PCEngine/Net Game (USA).state1' ] && [ ! -e '$T/share/states' ] && [ \$(find $T/share -type f | wc -l) = 1 ]" "its state is on the console, nothing is written to the share"
expect "grep -q 'last_rom=/smb/10.0.0.5/games/PCEngine/Net Game (USA).pce' $T/root/pce-ps5.ini" "the shelf remembers it"
nosan "$T"
# a power cycle (pause menu, Down six times) starts the card again from memory: the share is not read again
rc=$(PCEPS5_HOST_REALTIME=1 MAXFLIPS=900 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0;$(SEQ 280 $L3R3 $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $CROSS);400:$CROSS;440:0;$(QUITAT 480)" "430")
expect "[ $rc = 0 ] && grep -q 'power cycle: Net Game (USA).pce kept in memory, not read again' $T/root/logs/boot.log && [ \$(grep -c 'running \"Net Game (USA)\"' $T/root/logs/boot.log) = 2 ]" "a power cycle restarts the HuCard from memory, not from the share"
expect "$CHECK $T/dump/flip00430.ppm 960 540 green >/dev/null" "and it runs (Cross -> green)"
nosan "$T"
# b. CD games on an NFS share, the System Card on the console. The stand-in card (make_test_rom.py cdplay) plays the
# disc's track 2 in a loop: the core reads the disc through netfs while the game runs (netfs's blocks and read-ahead),
# and the tones come out in tune (check_tone.py)
for kind in cue chd; do
	T=$(newroot t32$kind)
	mkdir -p "$T/root/bios" "$T/share/PCEngineCD"
	$ROM "$T/root/bios/syscard3.pce" cdplay >/dev/null
	$CD "$T/share/PCEngineCD/Tone (USA).$kind" >/dev/null
	echo "nfs://10.0.0.5/export/pce" >"$T/root/shares.txt"
	rc=$(PCEPS5_HOST_AUDIO_DUMP=$T/audio.raw PCEPS5_HOST_REALTIME=1 MAXFLIPS=900 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0;300:$CROSS;340:0;$(QUITAT 400)" "290,330")
	expect "[ $rc = 0 ] && grep -q 'loading /nfs/10.0.0.5/export/pce/PCEngineCD/Tone (USA).$kind, System Card syscard3.pce' $T/root/logs/boot.log && grep -q 'running \"Tone (USA)\" (PC Engine CD)' $T/root/logs/boot.log" "$kind on a share: the disc starts as a PC Engine CD game"
	expect "$CHECK $T/dump/flip00290.ppm 960 540 red >/dev/null && $CHECK $T/dump/flip00330.ppm 960 540 green >/dev/null" "$kind on a share: the System Card runs (red), the pad reaches it (green)"
	expect "grep -q 'span(s) of the disc fetched ahead' $T/root/logs/boot.log && grep -q 'prefetch for a disc: .* bytes kept; the first: 2/2 block(s) here' $T/root/logs/boot.log" "$kind on a share: the start of the disc and of its tracks fetched before the game starts"
	expect "$TONE $T/audio.raw >$T/tone.txt" "$kind on a share: the disc's music, read from the share while the game runs: 1000 Hz left, 1500 Hz right, in tune and clean"
	sed 's/^/    /' $T/tone.txt
	nosan "$T"
done
# the same with 200 ms on every read, as a slow link: what was fetched ahead plays with no wait
T=$(newroot t32slow)
mkdir -p "$T/root/bios" "$T/share"
$ROM "$T/root/bios/syscard3.pce" cdplay >/dev/null
$CD "$T/share/Tone.cue" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_NETFS_DELAY_MS=200 PCEPS5_HOST_AUDIO_DUMP=$T/audio.raw PCEPS5_HOST_REALTIME=1 MAXFLIPS=1100 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0;$(QUITAT 700)" "")
expect "[ $rc = 0 ] && grep -q 'running \"Tone\" (PC Engine CD)' $T/root/logs/boot.log" "a slow link (200 ms a read): the disc starts"
expect "! grep -q 'waited for Tone.bin in realtime' $T/root/logs/boot.log" "and its track plays from what was fetched ahead: the game never waits for the share"
expect "$TONE $T/audio.raw >$T/tone.txt" "in tune and clean"
sed 's/^/    /' $T/tone.txt
nosan "$T"
# a slower link (2 s a read) and a 16 s track: the start of the disc is not all here when the game starts; the rest
# comes in while the System Card boots and the music plays, in tune
T=$(newroot t32slow2)
mkdir -p "$T/root/bios" "$T/share"
$ROM "$T/root/bios/syscard3.pce" cdplay >/dev/null
$CD "$T/share/Tone.cue" --long >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_NETFS_DELAY_MS=2000 PCEPS5_HOST_AUDIO_DUMP=$T/audio.raw PCEPS5_HOST_REALTIME=1 MAXFLIPS=1500 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0;$(QUITAT 1200)" "")
expect "[ $rc = 0 ] && grep -q 'running \"Tone\" (PC Engine CD)' $T/root/logs/boot.log && grep -q 'prefetch for a disc: .* the first: [01]/2 block(s) here' $T/root/logs/boot.log" "a slower link (2 s a read): the game starts before the start of its disc is all here"
expect "$TONE $T/audio.raw >$T/tone.txt" "and the track plays at its pitch as the rest of it arrives"
sed 's/^/    /' $T/tone.txt
nosan "$T"
# 1.1.1: where a .cue's tracks start, for the prefetch: each stretch between two INDEX lines has the sector size of its
# own track (one .bin mixing a 2048-byte data track with audio tracks), and in an .ogg (not laid out in sectors) only
# its start is fetched. Only the log is read: the disc itself is not a real one
T=$(newroot t32cue)
mkdir -p "$T/root/bios" "$T/share"
$ROM "$T/root/bios/syscard3.pce" cdplay >/dev/null
head -c $((1700 * 2352)) /dev/zero >"$T/share/Mixed.bin"
printf 'FILE "Mixed.bin" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 00 00:10:00\n    INDEX 01 00:12:00\n  TRACK 03 AUDIO\n    INDEX 01 00:20:00\nFILE "Mixed-04.ogg" OGG\n  TRACK 04 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n' >"$T/share/Mixed.cue"
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_REALTIME=1 MAXFLIPS=400 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0" "")
expect "grep -q 'ahead: Mixed.bin at 0$' $T/root/logs/boot.log && grep -q 'ahead: Mixed.bin at 1888800$' $T/root/logs/boot.log && grep -q 'ahead: Mixed.bin at 3300000$' $T/root/logs/boot.log && [ \$(grep -c 'ahead: Mixed.bin at' $T/root/logs/boot.log) = 3 ]" "a .bin mixing 2048- and 2352-byte tracks: its tracks' starts at the right bytes (750 x 2048 + 150 x 2352, + 600 x 2352)"
expect "grep -q 'ahead: Mixed-04.ogg at 0$' $T/root/logs/boot.log && [ \$(grep -c 'ahead: Mixed-04.ogg at' $T/root/logs/boot.log) = 1 ]" "an .ogg track: only its start"
nosan "$T"
# c. a share that does not connect: the shelf still works and says why
T=$(newroot t32f)
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_REALTIME=1 PCEPS5_HOST_NETFS_DIR="$T/nothing" run "$T" "0:0;100:$OPTIONS;102:0;110:$CROSS;112:0" "")
expect "[ $rc = 0 ] && grep -q '\[games\] 0 ROM(s)' $T/root/logs/boot.log" "a share that does not connect leaves the shelf working"
expect "grep -q '\[net\] The folder games is not on 10.0.0.5' $T/root/logs/boot.log" "and says why"
nosan "$T"
# d. shares.txt: written with its comments when missing; a bad line is named in the log, never its text
T=$(newroot t32n)
rc=$(run "$T" "0:0;$(SHELFQUIT_AT 40)" "")
expect "[ $rc = 0 ] && grep -q '^# Beetle PCE PS5 network shares' $T/root/shares.txt && ! grep -q '^smb://\|^nfs://' $T/root/shares.txt" "a commented shares.txt is written when there is none"
printf 'smb://bob@10.0.0.5/games?password=Hunter2x\nftp://10.0.0.5/x?password=Hunter2y\n' >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_NETFS_DIR="$T/nothing" run "$T" "0:0;$(SHELFQUIT_AT 40)" "")
expect "[ $rc = 0 ] && grep -q 'shares.txt line 2: ' $T/root/logs/boot.log" "a line that is not an address is named by its number"
expect "! grep -rq 'Hunter2' $T/root/logs $T/out.txt" "no password in a log or on stdout"
nosan "$T"
fi

if want 33; then
echo "== 33. a game on a share with a made-up file name is named by its CRC in the background, its cover follows; a zip's system too"
T=$(newroot t33)
PCEREPO=NEC_-_PC_Engine_-_TurboGrafx_16
mkdir -p "$T/srv/$PCEREPO/Named_Boxarts" "$T/share/misc"
python3 - "$T/srv/$PCEREPO/Named_Boxarts" <<'PY'
import sys
from PIL import Image
Image.new('RGB', (512, 357), (0, 255, 0)).save(sys.argv[1] + '/Splatterhouse (Japan).png')
open(sys.argv[1] + '/Splatterhouse (USA).png', 'w').write('Splatterhouse (Japan).png')
PY
$ROM "$T/share/splat.pce" >/dev/null
python3 tests/forge_crc.py "$T/share/splat.pce" $(grep -P "^pce\t[0-9A-F]{8}\tSplatterhouse \(USA\)$" frontend/data/nointro.tsv | cut -f2) >/dev/null
# a SuperGrafx card in a zip, in a folder whose name says nothing: listed as a PC Engine game until its zip is read
$ROM "$T/sgx.sgx" >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w'); z.write(sys.argv[2],'Super.sgx'); z.close()" "$T/share/misc/super.zip" "$T/sgx.sgx"
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
PORT=18082
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
rc=$(OFFLINE= COVER_URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png" PCEPS5_HOST_REALTIME=1 MAXFLIPS=420 PCEPS5_HOST_NETFS_DIR="$T/share" \
	run "$T" "0:0" "400")
kill $SRVPID 2>/dev/null
expect "grep -q '\[games\] naming 2 network ROM(s) by CRC' $T/root/logs/boot.log" "the pass was started for the two games the scan did not read"
expect "grep -q 'splat.pce \[pce\] -> \"Splatterhouse (USA)\" (by CRC)' $T/root/logs/boot.log" "the next scan names the HuCard by its CRC"
expect "grep -q 'misc/super.zip \[pce\]' $T/root/logs/boot.log && grep -q 'misc/super.zip \[sgx\]' $T/root/logs/boot.log" "the zip moves to the SuperGrafx once its directory was read"
expect "grep -q '/smb/10.0.0.5/games/splat.pce' $T/root/covers/crc-cache.txt && grep -q '^sys:/smb/10.0.0.5/games/misc/super.zip' $T/root/covers/crc-cache.txt" "both are kept for the next start"
expect "[ -f '$T/root/covers/PCEngine/Splatterhouse (USA).png' ]" "the cover of the game named late was downloaded"
nosan "$T"
# the next start reads nothing over the network: the cache names both
rc=$(PCEPS5_HOST_REALTIME=1 MAXFLIPS=200 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0" "")
expect "[ \$(grep -c 'splat.pce \[pce\] -> \"Splatterhouse (USA)\" (by CRC)' $T/root/logs/boot.log) -ge 1 ] && grep -q 'misc/super.zip \[sgx\]' $T/root/logs/boot.log && ! grep -q 'naming .* network ROM' $T/root/logs/boot.log" "the next start: named from the cache, no pass needed"
nosan "$T"
# a read error while the share stays connected (a link that drops, the watchdog, the shares connected again) is not the file's
# fault: nothing is remembered, the zip stays on the shelf, and the next start reads both again. A .cue sheet's track list is
# cached (a scan again doesn't read every sheet of the share once more): its track stays off the shelf when the sheet can't be read.
T=$(newroot t33e)
mkdir -p "$T/share/misc" "$T/share/PCEngineCD"
$ROM "$T/share/splat.pce" >/dev/null
python3 tests/forge_crc.py "$T/share/splat.pce" $(grep -P "^pce\t[0-9A-F]{8}\tSplatterhouse \(USA\)$" frontend/data/nointro.tsv | cut -f2) >/dev/null
$ROM "$T/sgx.sgx" >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w'); z.write(sys.argv[2],'Super.sgx'); z.close()" "$T/share/misc/super.zip" "$T/sgx.sgx"
printf 'FILE "Disc (Track 01).bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' >"$T/share/PCEngineCD/Disc (USA).cue"
head -c 4096 /dev/zero >"$T/share/PCEngineCD/Disc (Track 01).bin"
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_NETFS_EIO=splat.pce,super.zip PCEPS5_HOST_REALTIME=1 MAXFLIPS=400 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0" "")
expect "grep -q 'network CRC pass: 2 ROM(s) read\$' $T/root/logs/boot.log" "read errors: the pass tried both"
expect "! grep -q 'splat.pce.*fail' $T/root/covers/crc-cache.txt && ! grep -q '^sys:.*super.zip' $T/root/covers/crc-cache.txt" "and remembered no failure"
expect "[ \$(grep -c 'misc/super.zip \[pce\]' $T/root/logs/boot.log) -ge 2 ]" "the zip is still on the shelf after the pass"
expect "grep -q '^cue:/smb/10.0.0.5/games/PCEngineCD/Disc (USA).cue' $T/root/covers/crc-cache.txt && grep -q 'PCEngineCD/Disc (USA).cue \[pcecd\]' $T/root/logs/boot.log && ! grep -q 'Track 01).bin \[' $T/root/logs/boot.log" "the sheet's track list is cached, its track is not a game"
nosan "$T"
rc=$(PCEPS5_HOST_NETFS_EIO=.cue PCEPS5_HOST_REALTIME=1 MAXFLIPS=400 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0" "")
expect "grep -q 'splat.pce \[pce\] -> \"Splatterhouse (USA)\" (by CRC)' $T/root/logs/boot.log && grep -q 'misc/super.zip \[sgx\]' $T/root/logs/boot.log" "the next start reads both again and names them"
expect "grep -q 'PCEngineCD/Disc (USA).cue \[pcecd\]' $T/root/logs/boot.log && ! grep -q 'Track 01).bin \[' $T/root/logs/boot.log" "the sheet is not read again (it can't be now): its track is still not a game"
nosan "$T"
# Settings (Triangle) while the pass reads a ROM (3 s a read): the pass stops first (Settings > Network shares may connect the shares
# again, which needs no other netfs call running); the ROM it was reading is read again once Settings is left
T=$(newroot t33s)
mkdir -p "$T/share"
$ROM "$T/share/splat.pce" >/dev/null
python3 tests/forge_crc.py "$T/share/splat.pce" $(grep -P "^pce\t[0-9A-F]{8}\tSplatterhouse \(USA\)$" frontend/data/nointro.tsv | cut -f2) >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_NETFS_DELAY_MS=3000 PCEPS5_HOST_REALTIME=1 MAXFLIPS=900 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;60:$TRIANGLE;62:0;90:$CIRCLE;92:0" "")
expect "grep -q 'network CRC pass: 0 ROM(s) read (stopped)' $T/root/logs/boot.log" "Settings stops the pass while it reads"
expect "grep -q 'splat.pce \[pce\] -> \"Splatterhouse (USA)\" (by CRC)' $T/root/logs/boot.log" "and, once Settings is left, the ROM it was reading is read again and named"
nosan "$T"
fi

if want 34; then
echo "== 34. the Network shares screen: add, change, test and remove a share with the on-screen keyboard (Settings, Network shares)"
# tests/osk_pad.py turns the steps into a pad script (the keyboard's layout is its model). Settings is Triangle on the shelf; Up five
# times from Shader reaches Network shares (Back, Updates, Debug logs, Language, Network shares). Every run ends: Circle twice,
# OPTIONS, Cross.
net_pad() { python3 tests/osk_pad.py 2>/dev/null; }
net_quit='press CIRCLE 2
press OPTIONS
wait 8
press CROSS'
# a. a new share: server, share, folder with a space, user, a password that needs Shift and both kinds of symbols
T=$(newroot t34)
mkdir -p "$T/share"
PAD=$(net_pad <<STEPS
frame 40
press TRIANGLE
press UP 5
press CROSS
press CROSS
field 10.0.0.5
press DOWN
field games
press DOWN
field PC Engine
press DOWN
field jason
press DOWN
field Zq9@pw&1
press DOWN 6
press CROSS
wait 20
$net_quit
STEPS
)
rc=$(PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "$PAD" "")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "[ \"\$(tail -n 1 $T/root/shares.txt)\" = 'smb://jason@10.0.0.5/games/PC%20Engine?password=Zq9%40pw%261' ]" "the share is written as one escaped line"
expect "grep -q '^# Beetle PCE PS5 network shares' $T/root/shares.txt" "after the comments the app wrote"
expect "grep -q '\[net\] shares.txt: saved smb://jason@10.0.0.5/games/PC%20Engine' $T/root/logs/boot.log" "the log says what was saved"
expect "grep -q '\[net\] Connected to smb://jason@10.0.0.5/games/PC%20Engine' $T/root/logs/boot.log" "and the share connects at once, without a restart"
expect "! grep -rqE 'Zq9|pw%26' $T/root/logs $T/out.txt" "the password is in no log and not on stdout"
nosan "$T"
# b. change one share (its note stays), test the connection, remove the other
T=$(newroot t34b)
mkdir -p "$T/share/Sub" && echo x >"$T/share/a.pce"
printf '# my shares\nsmb://old/x   # my note\nnfs://nas/export/pce\n' >"$T/root/shares.txt"
PAD=$(net_pad <<STEPS
frame 40
press TRIANGLE
press UP 5
press CROSS
press CROSS
press DOWN 3
field bob
press DOWN 6
press CROSS
wait 120
press DOWN
press CROSS
wait 20
press DOWN
press CROSS
press DOWN 7
press CROSS
wait 8
press UP
press CROSS
wait 20
$net_quit
STEPS
)
rc=$(PCEPS5_HOST_REALTIME=1 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "$PAD" "")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "[ \"\$(cat $T/root/shares.txt)\" = \"\$(printf '# my shares\nsmb://bob@old/x   # my note')\" ]" "one line changed (note kept), the other was removed, the comment stays"
expect "grep -q '\[net\] test of smb://bob@old/x: Connected. The folder has 1 file and 1 folder.' $T/root/logs/boot.log" "Test connection reports what is in the folder"
expect "grep -q '\[net\] shares.txt: removed nfs://nas/export/pce' $T/root/logs/boot.log" "the removal is logged"
nosan "$T"
# c. Save with nothing filled in changes nothing
T=$(newroot t34c)
PAD=$(net_pad <<STEPS
frame 40
press TRIANGLE
press UP 5
press CROSS
press CROSS
press DOWN 10
press CROSS
wait 8
press CIRCLE
$net_quit
STEPS
)
rc=$(run "$T" "$PAD" "")
expect "[ $rc = 0 ] && ! grep -q '^smb://\|^nfs://' $T/root/shares.txt" "an empty form is not saved (and Circle on a form with no changes leaves it at once)"
nosan "$T"
# d. over a game (Settings from the pause menu) the row is there but can't be chosen (the game may be reading from a share):
# the pause menu: Down four times to Settings; there, Up five times from Shader passes Network shares and lands on
# Download covers, which Cross switches off; Circle, then Up five times to Quit
T=$(newroot t34d)
$ROM "$T/root/roms/test.pce" >/dev/null
rc=$(MAXFLIPS=900 run "$T" "0:0;$(SEQ 100 $L3R3 $DOWN $DOWN $DOWN $DOWN $CROSS $UP $UP $UP $UP $UP $CROSS $CIRCLE $UP $UP $UP $UP $UP $CROSS)" "" "$T/root/roms/test.pce")
expect "[ $rc = 0 ] && grep -q '^covers_download=0' $T/root/pce-ps5.ini" "over a game: Network shares is skipped (Up five times reaches Download covers)"
nosan "$T"
fi

if want 35; then
echo "== 35. shelf tabs: Internal and Network apart, L1 / R1 between them, the last tab kept, Square on Network scans again; L2 / R2 skip 10"
# The tab bar sits at the top: the open tab's pill is the accent colour (78,62,170), the other one dark (32,30,58); (795,30) is
# inside the Internal pill and (1120,30) inside the Network pill (above their names). The share connects within a few seconds of wall time.
tab_pills() { # dump the active tab: Internal | Network
	local f=$1 on=$2
	if [ "$on" = Internal ]; then
		$CHECK "$f" 795 30 78 62 170 >/dev/null && $CHECK "$f" 1120 30 32 30 58 >/dev/null
	else
		$CHECK "$f" 795 30 32 30 58 >/dev/null && $CHECK "$f" 1120 30 78 62 170 >/dev/null
	fi
}
T=$(newroot t35)
mkdir -p "$T/share"
$ROM "$T/root/roms/Local Game (USA).pce" >/dev/null
$ROM "$T/share/Net Game (USA).pce" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_REALTIME=1 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;220:$R1;222:0;300:$SQUARE;302:0;380:$L1;382:0;420:$OPTIONS;422:0;430:$CROSS;432:0" "200,260,400")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "tab_pills $T/dump/flip00200.ppm Internal" "the shelf opens on the Internal tab"
expect "tab_pills $T/dump/flip00260.ppm Network" "R1 goes to the Network tab"
expect "tab_pills $T/dump/flip00400.ppm Internal" "L1 comes back"
expect "grep -q '\[shelf\] tab Network: 1 game' $T/root/logs/boot.log" "the Network tab holds the game on the share"
expect "grep -q '\[shelf\] tab Internal: 1 game' $T/root/logs/boot.log" "and the Internal tab only the local one"
expect "grep -q '\[shelf\] network tab: scan again\$' $T/root/logs/boot.log" "Square on the Network tab scans again"
expect "[ \$(grep -c '\[games\] 2 ROM' $T/root/logs/boot.log) -ge 2 ]" "and the scan found both games again"
expect "awk '/network tab: scan again/{f=1;next} f&&/\[covers\]/{c++} f&&/\[shelf\] tab /{exit} END{exit !(f && c == 0)}' $T/root/logs/boot.log" "the same games after the scan: the covers are not loaded again"
expect "grep -q 'last_tab=0' $T/root/pce-ps5.ini" "the tab left on is kept (Internal)"
nosan "$T"
T=$(newroot t35b)
mkdir -p "$T/share"
$ROM "$T/root/roms/Local Game (USA).pce" >/dev/null
$ROM "$T/share/Net Game (USA).pce" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
echo "last_tab=2" >>"$T/root/pce-ps5.ini"
rc=$(PCEPS5_HOST_REALTIME=1 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;300:$OPTIONS;302:0;310:$CROSS;312:0" "40,250")
expect "[ $rc = 0 ] && tab_pills $T/dump/flip00040.ppm Network" "with last_tab=2 and a share set up the shelf opens on Network, before the share is up"
expect "tab_pills $T/dump/flip00250.ppm Network" "and stays there when its games arrive"
expect "grep -q 'last_tab=2' $T/root/pce-ps5.ini" "and keeps the tab on quit"
T=$(newroot t35c)
$ROM "$T/root/roms/Local Game (USA).pce" >/dev/null
echo "last_tab=2" >>"$T/root/pce-ps5.ini"
rc=$(run "$T" "0:0;60:$R1;62:0;70:$SQUARE;72:0;90:$L1;92:0;120:$OPTIONS;122:0;130:$CROSS;132:0" "40,80,110")
expect "[ $rc = 0 ] && tab_pills $T/dump/flip00040.ppm Internal" "last_tab=2 without a share set up opens on the games instead"
expect "tab_pills $T/dump/flip00080.ppm Network && grep -q '\[shelf\] tab Network: 0 game' $T/root/logs/boot.log" "R1 shows the empty Network tab"
expect "grep -q '\[shelf\] network tab: scan again\$' $T/root/logs/boot.log" "Square on it scans again, with no share to ask"
expect "tab_pills $T/dump/flip00110.ppm Internal && grep -q 'last_tab=0' $T/root/pce-ps5.ini" "L1 goes back and the tab is kept"
nosan "$T"
T=$(newroot t35d)
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_REALTIME=1 PCEPS5_HOST_NETFS_DIR="$T/nothing" run "$T" "0:0;150:$SQUARE;152:0;330:$OPTIONS;332:0;340:$CROSS;342:0" "100")
expect "[ $rc = 0 ] && tab_pills $T/dump/flip00100.ppm Network" "a share that is down leaves a shelf with no games on the Network tab"
expect "grep -q '\[shelf\] network tab: scan again, reconnecting' $T/root/logs/boot.log" "Square connects it again"
expect "[ \$(grep -c 'The folder games is not on 10.0.0.5' $T/root/logs/boot.log) -ge 2 ]" "and says why each time"
nosan "$T"
# the system tabs (Up / Down) work inside the storage tab; L2 / R2 skip 10 games (L1 / R1 did before 1.1)
T=$(newroot t35e)
mkdir -p "$T/share"
for i in $(seq -w 1 12); do $ROM "$T/root/roms/Game $i (USA).pce" >/dev/null; done
$ROM "$T/root/roms/Super (Japan).sgx" >/dev/null
$ROM "$T/share/Net Super (Japan).sgx" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
# (back to the shelf from a game: the pause menu, Up twice, Cross)
rc=$(PCEPS5_HOST_REALTIME=1 MAXFLIPS=900 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$R2;152:0;170:$CROSS;172:0;$(SEQ 230 $L3R3 $UP $UP $CROSS);320:$R1;322:0;340:$DOWN;342:0;380:$CROSS;382:0;$(QUITAT 480)" "")
expect "[ $rc = 0 ] && grep -q 'loading .*/Game 11 (USA).pce' $T/root/logs/boot.log" "R2 skips 10 games (Game 01 -> Game 11)"
expect "grep -q '\[shelf\] tab SuperGrafx: 1 game' $T/root/logs/boot.log && grep -q 'loading /smb/10.0.0.5/games/Net Super (Japan).sgx' $T/root/logs/boot.log" "on the Network tab, Down shows its SuperGrafx game, which runs"
nosan "$T"
fi

# ---- 7-Zip (1.1): HuCards inside a .7z (frontend/fe_7z.cpp, the LZMA SDK's C decoder); the archives are tests/7z/ ------
if want 36; then
echo "== 36. 7z: the reader on every kind of archive; a .7z on the shelf named and covered as its .zip twin, started, run; what is refused, and why; a .7z on a share"
SZ_OUT=$(ASAN_OPTIONS=detect_leaks=1 timeout 120 "$PWD/build/host/test-7z" "$PWD/tests/7z" "$PWD/build/host/test-7z-card.pce" 2>&1)
SZ_RC=$?
echo "$SZ_OUT" | tail -3 | sed 's/^/  /'
expect "[ $SZ_RC = 0 ]" "the 7z reader: LZMA, LZMA2, PPMd, BCJ, Delta, a solid block, the directory's CRCs; empty, truncated, corrupt, BZip2, AES, too big: every check passes"
expect "! echo \"\$SZ_OUT\" | grep -q 'runtime error\|Sanitizer'" "no sanitizer reports"
# a. the same card as a .7z and as a .zip: both named by the CRC their directory keeps, one cover for both; the .7z runs
T=$(newroot t36)
PCEREPO=NEC_-_PC_Engine_-_TurboGrafx_16
mkdir -p "$T/srv/$PCEREPO/Named_Boxarts" "$T/root/roms/PCEngine"
python3 -c "from PIL import Image; import sys; Image.new('RGB', (512, 357), (0, 255, 0)).save(sys.argv[1])" "$T/srv/$PCEREPO/Named_Boxarts/Splatterhouse (USA).png"
cp tests/7z/named.7z "$T/root/roms/PCEngine/seven.7z"
$ROM "$T/splat.pce" >/dev/null
python3 tests/forge_crc.py "$T/splat.pce" $(grep -P "^pce\t[0-9A-F]{8}\tSplatterhouse \(USA\)$" frontend/data/nointro.tsv | cut -f2) >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED); z.write(sys.argv[2],'splat.pce'); z.close()" "$T/root/roms/PCEngine/zipped.zip" "$T/splat.pce"
PORT=18083
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
# the shelf: the two games (same title: by path, seven.7z first); the 7z is selected, Cross starts it
rc=$(OFFLINE= COVER_URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png" PCEPS5_HOST_REALTIME=1 MAXFLIPS=900 run "$T" \
	"0:0;150:$CROSS;152:0;280:$CROSS;320:0;$(QUITAT 400)" "140,300")
kill $SRVPID 2>/dev/null
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'PCEngine/seven.7z \[pce\] -> \"Splatterhouse (USA)\" (by CRC)' $T/root/logs/boot.log && grep -q 'PCEngine/zipped.zip \[pce\] -> \"Splatterhouse (USA)\" (by CRC)' $T/root/logs/boot.log" "the .7z is named by its CRC, as its .zip twin"
expect "[ -f '$T/root/covers/PCEngine/Splatterhouse (USA).png' ] && $CHECK $T/dump/flip00140.ppm 960 420 0 255 0 >/dev/null" "and shows the cover the .zip gets"
expect "grep -q 'loading .*seven.7z -> splat.pce' $T/root/logs/boot.log && grep -q 'running \"seven\" (PC Engine)' $T/root/logs/boot.log" "the card inside the .7z is loaded"
expect "$CHECK $T/dump/flip00300.ppm 960 540 green >/dev/null" "and runs as the .zip's does (Cross -> green)"
expect "grep -q '^sys:.*seven.7z' $T/root/covers/crc-cache.txt" "the .7z's system is cached as a zip's"
nosan "$T"
# b. a SuperGrafx card after a text file in the archive: its system from the card inside; started as SuperGrafx
T=$(newroot t36b)
cp tests/7z/sgx.7z "$T/root/roms/super.7z"
rc=$(run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 200)" "90,130" "$T/root/roms/super.7z")
expect "[ $rc = 0 ] && grep -q 'loading .*super.7z -> Super (Japan).sgx' $T/root/logs/boot.log && grep -q 'running \"super\" (SuperGrafx)' $T/root/logs/boot.log" "a .sgx in a .7z starts as a SuperGrafx game"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null && $CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "it runs"
nosan "$T"
# c. what the shelf leaves out (a CD image, no game, a broken directory) and what says why it can't start
T=$(newroot t36c)
for f in disc nogame empty truncated bzip2 aes aeshdr big corrupt lzma; do cp tests/7z/$f.7z "$T/root/roms/$f.7z"; done
rc=$(run "$T" "0:0;$(SHELFQUIT_AT 60)" "")
expect "[ $rc = 0 ] && grep -q '\[games\] 5 ROM(s)' $T/root/logs/boot.log" "the shelf lists the five archives with a HuCard in a readable directory"
expect "! grep -q 'disc.7z \[\|nogame.7z \[\|empty.7z \[\|truncated.7z \[\|aeshdr.7z \[' $T/root/logs/boot.log" "a CD image in a .7z, an archive with no HuCard, a cut or name-encrypted one: not listed"
nosan "$T"
for f in bzip2:"can't unpack" aes:"is encrypted" big:"too big" corrupt:"damaged"; do
	n=${f%%:*}; why=${f#*:}
	rc=$(MAXFLIPS=300 run "$T" "0:0;60:$CROSS;62:0" "50" "$T/root/roms/$n.7z")
	expect "grep -q \"could not start .*$n.7z: .*$why\" $T/root/logs/boot.log && [ -f $T/dump/flip00050.ppm ]" "$n.7z: not started; the screen says why ($why)"
	nosan "$T"
done
# d. a .7z on a share: listed before it is opened (its folder's name), named by its CRC in the background, started from the share
T=$(newroot t36d)
mkdir -p "$T/share/PCEngine"
cp tests/7z/named.7z "$T/share/PCEngine/seven.7z"
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(PCEPS5_HOST_REALTIME=1 MAXFLIPS=900 PCEPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;300:$CROSS;302:0;430:$CROSS;470:0;$(QUITAT 520)" "450")
expect "[ $rc = 0 ] && grep -q '\[games\] naming 1 network ROM(s) by CRC' $T/root/logs/boot.log" "the .7z on the share is named in the background (the scan doesn't open it)"
expect "grep -q 'PCEngine/seven.7z \[pce\] -> \"Splatterhouse (USA)\" (by CRC)' $T/root/logs/boot.log" "by the CRC its directory keeps"
expect "grep -q 'loading /smb/10.0.0.5/games/PCEngine/seven.7z -> splat.pce' $T/root/logs/boot.log && $CHECK $T/dump/flip00450.ppm 960 540 green >/dev/null" "it starts from the share and runs"
nosan "$T"
fi

echo
echo "passed $PASS, failed $FAIL  (work dir $WORK)"
[ $FAIL = 0 ]
