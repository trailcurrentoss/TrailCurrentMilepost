#!/bin/bash
# Build Milepost firmware.
# Produces two binaries:
#   build/milepost.bin         — app-only (for OTA via Headwaters)
#   build/milepost_merged.bin  — merged  (for web flasher, full flash at 0x0)
#
# The app-only binary contains just the application image. Headwaters OTA
# writes it to a single app partition via esp_ota_write, which validates
# the image as an app. A merged binary would fail that validation because
# it starts with the bootloader, not an app header.
#
# The merged binary combines bootloader + partition table + OTA data + app
# into one file flashable at offset 0x0. The web flasher requires this
# because it writes the entire flash from a single binary.
set -e

OUTPUT_DIR="build"

echo "========================================"
echo "Building Milepost ..."
echo "========================================"
idf.py build

# Create merged binary (for web flasher — flashable at 0x0)
esptool.py --chip esp32s3 merge_bin -o "$OUTPUT_DIR/milepost_merged.bin" \
    --flash_mode dio --flash_size 16MB \
    0x0 "$OUTPUT_DIR/bootloader/bootloader.bin" \
    0x8000 "$OUTPUT_DIR/partition_table/partition-table.bin" \
    0xe000 "$OUTPUT_DIR/ota_data_initial.bin" \
    0x10000 "$OUTPUT_DIR/milepost.bin"

echo ""
echo "========================================"
echo "Build complete"
echo "========================================"
echo ""
echo "App-only binary (for OTA):"
ls -lh "$OUTPUT_DIR/milepost.bin"
echo ""
echo "Merged binary (for web flasher):"
ls -lh "$OUTPUT_DIR/milepost_merged.bin"
echo ""
echo "Attach BOTH binaries to the GitHub release."
