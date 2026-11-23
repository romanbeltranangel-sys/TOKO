#!/usr/bin/env bash
# Compila el firmware de Troko (ST7735) con ESP-IDF 6.1 instalado en tu equipo.
set -e
if ! command -v idf.py >/dev/null 2>&1; then
  for p in "$IDF_PATH" "$HOME/.espressif/v6.1/esp-idf" "$HOME/esp/esp-idf"; do
    if [ -n "$p" ] && [ -f "$p/export.sh" ]; then source "$p/export.sh"; break; fi
  done
fi
command -v idf.py >/dev/null 2>&1 || { echo "No encuentro ESP-IDF 6.1. Instálalo o usa compilar-docker.sh"; exit 1; }
python3 scripts/build.py wk-esp32s3-dev --name troko-st7735 \
  --language es-ES --custom-wake-word troko --custom-wake-word-display Troko
echo
echo "Listo: build/merged-binary.bin  (se graba en la dirección 0x0)"
