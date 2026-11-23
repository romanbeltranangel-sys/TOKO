@echo off
REM Compila con Docker Desktop (Windows). No hace falta instalar ESP-IDF.
docker run --rm -v "%cd%":/project -w /project espressif/idf:release-v6.1 bash -c "git config --global --add safe.directory /project; python scripts/build.py wk-esp32s3-dev --name troko-st7735 --language es-ES --custom-wake-word troko --custom-wake-word-display Troko"
echo.
echo Listo: build\merged-binary.bin  (se graba en la direccion 0x0)
pause
