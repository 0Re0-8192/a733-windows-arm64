#!/usr/bin/env bash
set -euo pipefail

BUILD_ROOT=$HOME/a733-usb-shortening-build
OUT_ROOT=$PWD/artifacts

echo "A733 auto-UMS U-Boot build started: $(date -Is)"

test -d "$BUILD_ROOT/tf-a"
test -d "$BUILD_ROOT/u-boot"
test -s "$BUILD_ROOT/tf-a/build/sun60i_a733/debug/bl31.bin"

mkdir -p "$OUT_ROOT"

cd "$BUILD_ROOT/u-boot"
export BL31="$BUILD_ROOT/tf-a/build/sun60i_a733/debug/bl31.bin"

make CROSS_COMPILE=aarch64-linux-gnu- NO_PYTHON=1 radxa-cubie-a7a_defconfig
./scripts/config --disable TOOLS_MKEFICAPSULE
./scripts/config --enable CMD_USB_MASS_STORAGE

# Make this FEL-loaded build self-driving:
# - ignore any saved uboot.env on the SD card
# - immediately expose the SD card as USB Mass Storage
./scripts/config --enable ENV_IS_NOWHERE
./scripts/config --disable ENV_IS_IN_FAT
./scripts/config --enable USE_BOOTCOMMAND
./scripts/config --set-val BOOTDELAY 0
./scripts/config --set-str BOOTCOMMAND 'ums 0 mmc 0'

make CROSS_COMPILE=aarch64-linux-gnu- NO_PYTHON=1 olddefconfig
make CROSS_COMPILE=aarch64-linux-gnu- NO_PYTHON=1 -j"$(nproc)"

test -s u-boot-sunxi-with-spl.bin
cp -f u-boot-sunxi-with-spl.bin "$OUT_ROOT/u-boot-sunxi-with-spl-a733-auto-ums.bin"

if [ -s u-boot-sunxi-with-spl.fit.fit ]; then
  cp -f u-boot-sunxi-with-spl.fit.fit "$OUT_ROOT/u-boot-sunxi-with-spl-a733-auto-ums.fit.fit"
fi

if [ -s u-boot.itb ]; then
  cp -f u-boot.itb "$OUT_ROOT/u-boot-a733-auto-ums.itb"
fi

echo "Auto-UMS artifact checks:"
grep -E 'CONFIG_(ENV_IS_NOWHERE|ENV_IS_IN_FAT|USE_BOOTCOMMAND|BOOTDELAY|BOOTCOMMAND|CMD_USB_MASS_STORAGE|USB_FUNCTION_MASS_STORAGE)=' .config || true
ls -l "$OUT_ROOT"/u-boot-*auto-ums* 2>/dev/null || true
sha256sum "$OUT_ROOT"/u-boot-*auto-ums* 2>/dev/null || true

echo "A733 auto-UMS U-Boot build finished: $(date -Is)"
