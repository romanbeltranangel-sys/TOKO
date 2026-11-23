#!/usr/bin/env bash
# Compila con Docker (no hace falta instalar ESP-IDF). Requiere Docker.
set -e
docker run --rm -v "$PWD":/project -w /project espressif/idf:release-v6.1 \
  bash -c 'git config --global --add safe.directory /project; python scripts/build.py wk-esp32s3-dev --name troko-st7735 --language es-ES --custom-wake-word troko --custom-wake-word-display Troko'
echo
echo "Listo: build/merged-binary.bin  (se graba en la dirección 0x0)"
