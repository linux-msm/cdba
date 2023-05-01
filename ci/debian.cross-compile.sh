#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018-2020 Petr Vorel <pvorel@suse.cz>
# Copyright (c) 2021 Canonical Ltd.
# Copyright (c) 2023 Linaro Ltd
# Author: Krzysztof Kozlowski <krzysztof.kozlowski@linaro.org>
#                             <krzk@kernel.org>
#

set -ex

if [ -z "$ARCH" ]; then
	echo "missing \$ARCH!" >&2
	exit 1
fi

case "$ARCH" in
	armel) PKGS_CC="gcc-arm-linux-gnueabi libc6-dev-${ARCH}-cross";;
	arm64) PKGS_CC="gcc-aarch64-linux-gnu libc6-dev-${ARCH}-cross";;
	ppc64el) PKGS_CC="gcc-powerpc64le-linux-gnu libc6-dev-${ARCH}-cross";;
	# TODO: libraries for riscv?
	#riscv64) PKGS_CC="gcc-riscv64-linux-gnu libc6-dev-${ARCH}-cross";;
	s390x) PKGS_CC="gcc-${ARCH}-linux-gnu libc6-dev-${ARCH}-cross";;
	*) echo "unsupported arch: '$ARCH'!" >&2; exit 1;;
esac

dpkg --add-architecture $ARCH
apt update

apt install -y --no-install-recommends \
	libftdi-dev:${ARCH} \
	libudev-dev:${ARCH} \
	libyaml-dev:${ARCH} \
	$PKGS_CC

echo "Install finished: $0"
