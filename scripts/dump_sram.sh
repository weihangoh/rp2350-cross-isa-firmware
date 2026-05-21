#!/usr/bin/env bash
set -euo pipefail

OUTDIR="${1:-dumps}"
ADDR="${2:-0x20070000}"
SIZE="${3:-0x10000}"

mkdir -p "$OUTDIR"

STAMP="$(date +%Y%m%d_%H%M%S)"
OUTFILE="$OUTDIR/bcdemo_ctx_${STAMP}.bin"

echo "[*] Dumping SRAM"
echo "    address: $ADDR"
echo "    size:    $SIZE"
echo "    output:  $OUTFILE"

sudo openocd \
  -f interface/cmsis-dap.cfg \
  -f target/rp2350.cfg \
  -c "adapter speed 1000" \
  -c "init" \
  -c "halt" \
  -c "dump_image $OUTFILE $ADDR $SIZE" \
  -c "resume" \
  -c "shutdown"

echo "[*] Dump complete."
ls -lh "$OUTFILE"

echo "[*] First 256 bytes:"
xxd -g 4 -l 256 "$OUTFILE"
