#!/usr/bin/env bash
# Cross-build qpic_drm_demo for U60Pro (aarch64 musl, static).
#
# Primary: musl.cc toolchain + Alpine Docker (works when dockcross pull 401s).
# Optional: dockcross/linux-arm64-musl if DOCKCROSS=1 and image is reachable.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

OUT=qpic_drm_demo
SRC=qpic_drm_demo.c
TOOLCHAIN_DIR="$ROOT/aarch64-linux-musl-cross"
TOOLCHAIN_TGZ="${TOOLCHAIN_TGZ:-aarch64-linux-musl-cross.tgz}"
TOOLCHAIN_URL="${TOOLCHAIN_URL:-https://musl.cc/${TOOLCHAIN_TGZ}}"

build_with_musl_cc() {
	if [[ ! -x "$TOOLCHAIN_DIR/bin/aarch64-linux-musl-gcc" ]]; then
		echo "==> fetching $TOOLCHAIN_URL"
		curl -fsSL -o "$TOOLCHAIN_TGZ" "$TOOLCHAIN_URL"
		rm -rf "$TOOLCHAIN_DIR"
		tar xf "$TOOLCHAIN_TGZ"
	fi

	echo "==> compiling via Alpine Docker + musl.cc toolchain"
	docker run --platform linux/amd64 --rm -v "$ROOT:/work" -w /work alpine:3.20 sh -c "
		set -e
		CC=./aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc
		HDR=./aarch64-linux-musl-cross/aarch64-linux-musl/include/drm
		\$CC -O2 -Wall -Wextra -static -I\$HDR '$SRC' -o '$OUT'
	"
}

build_with_dockcross() {
	local image="${DOCKCROSS_IMAGE:-dockcross/linux-arm64-musl}"
	local wrapper="$ROOT/dockcross-arm64-musl"

	echo "==> docker image: $image"
	docker pull "$image"
	if [[ ! -x "$wrapper" ]]; then
		docker run --rm "$image" > "$wrapper"
		chmod +x "$wrapper"
	fi

	echo "==> compiling (dockcross static aarch64-musl)"
	"$wrapper" bash -c "
		set -e
		INC=''
		for d in /usr/include/libdrm /usr/include/drm /usr/include; do
			[[ -f \"\$d/drm.h\" || -f \"\$d/drm/drm.h\" ]] && INC=\"\$INC -I\$d\"
		done
		[[ -d /usr/include/libdrm ]] && INC=\"\$INC -I/usr/include/libdrm\"
		\$CC -O2 -Wall -Wextra -static \$INC '$SRC' -o '$OUT'
	"
}

if [[ "${DOCKCROSS:-0}" == "1" ]]; then
	build_with_dockcross
else
	build_with_musl_cc
fi

echo "==> done: $ROOT/$OUT"
file "$OUT" 2>/dev/null || true
ls -la "$OUT"
