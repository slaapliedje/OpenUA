#!/usr/bin/env bash
#
# capture_encode.sh — turn a raw capture from capture_demo.sh into the AVI a
# person actually watches.
#
#   tools/capture_encode.sh <target> <start-seconds> [outdir]
#
# `start-seconds` is where the main menu appears in the RAW file, and it has to
# be read off the raw file per target rather than computed: the recording starts
# at power-on, and TOS/Kickstart boot plus the engine's own load take a
# different amount of time on every machine. Sample the raw file first —
#
#   ffmpeg -i <raw> -vf fps=1/8,scale=320:-1 /tmp/f/%03d.png && montage ...
#
# — and read the timestamp off the first main-menu frame.
#
# ★ The timeline is NOT resampled. No -r, no fps filter, no setpts: the whole
# value of these files is that a walk step takes as long on screen as it takes
# on the machine, and every one of those options would quietly rescale exactly
# that. Trimming and padding are safe because they do not touch frame timing.
#
# The codec is LOSSLESS on purpose. These frames are 4-to-8-bit pixel art
# scaled with nearest-neighbour, the worst case for a DCT codec: mpeg4 at any
# sane bitrate rings around every hard edge, and that ringing reads as a
# rendering bug in a video whose whole job is to show rendering.
#
# libx264rgb -qp 0, not ffv1, and the difference is enormous here: ffv1 is
# INTRA-ONLY, so a screen that has not changed for four seconds is re-encoded
# in full 240 times. The first Falcon encode came out at 731 MB. Lossless x264
# predicts across frames, so a static screen costs almost nothing — the same
# footage, bit-identical, lands around 13 MB. With five of these to hand over,
# that is the difference between a link and an apology.
#
set -euo pipefail

TARGET="${1:?usage: capture_encode.sh <target> <start-seconds> [outdir]}"
START="${2:?need the start offset in seconds (main menu in the RAW file)}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${3:-$REPO/data/work/capture}"
RAW="$OUTDIR/$TARGET-raw.avi"
OUT="$OUTDIR/openua-$TARGET.avi"
FONT=/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf

case "$TARGET" in
falcon) LABEL="Atari Falcon030   16 MHz 68030   VIDEL, 8-bit chunky" ;;
tt)     LABEL="Atari TT030   32 MHz 68030   TT-shifter, 8 bitplanes" ;;
ste)    LABEL="Atari STe   8 MHz 68000   4 bitplanes, native planar" ;;
aga)    LABEL="Amiga 1200 AGA   14 MHz 68020   8 bitplanes, native planar" ;;
ecs)    LABEL="Amiga ECS   7 MHz 68000   5 bitplanes, native planar" ;;
*)      echo "unknown target: $TARGET" >&2; exit 1 ;;
esac

[[ -f "$RAW" ]] || { echo "no raw capture at $RAW" >&2; exit 1; }

# Normalise to a common 640x480 stage so the five files can sit side by side:
# scale to fit by the LONGER constraint, nearest-neighbour (these are hard-edged
# 320x200-class frames — bilinear would blur the very pixels being judged), then
# letterbox. Machines differ in native size: 640x480 on the Falcon, 320x200 on
# the STe, 720x568 on the Amiga window.
# The Amiga grab is an X11 window rectangle, not a framebuffer, so it carries a
# black margin the Amiga never drew — amiberry letterboxes a 320x200-class
# display inside a 720x568 window. Crop to the pixels the machine actually
# produced, or the scale-to-fit below shrinks the picture to pay for margin.
# Measured with `ffmpeg -vf cropdetect` on the raw, not guessed.
CROP=""
case "$TARGET" in
aga|ecs) CROP="crop=610:374:72:50," ;;
esac

VF="${CROP}scale=640:480:force_original_aspect_ratio=decrease:flags=neighbor"
VF="$VF,pad=640:520:(ow-iw)/2:(480-ih)/2:color=black"
# The clock sits left, the label starts clear of it. Centring the label looked
# right on the Falcon and collided with the timer on the Amiga, whose label is
# longer — a fixed x is one less thing that depends on the string.
VF="$VF,drawtext=fontfile=$FONT:text='%{pts\\:hms}':x=8:y=496:fontsize=14:fontcolor=yellow"
VF="$VF,drawtext=fontfile=$FONT:text='$LABEL':x=136:y=496:fontsize=14:fontcolor=white"

# Hatari's AVI carries a PCM track (the emulated YM/DMA output); the Amiga
# x11grab captures are video-only. Keep whichever is there rather than forcing
# either — re-encoded to mp3 because 44.1 kHz stereo PCM would outweigh the
# lossless VIDEO by 2:1.
AUDIO=(-an)
if ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 "$RAW" | grep -q .; then
	AUDIO=(-c:a libmp3lame -b:a 128k)
fi

echo "encode[$TARGET]: $RAW  +${START}s -> $OUT"
ffmpeg -v error -y -ss "$START" -i "$RAW" -vf "$VF" \
       -c:v libx264rgb -qp 0 -preset veryfast "${AUDIO[@]}" "$OUT"
ffprobe -v error -show_entries format=duration,size -of default=nw=1 "$OUT"
