#!/usr/bin/env bash
# Prepare the vendor inputs under vendor/ from the official HiSilicon
# Hi3516CV610 SDK repository:
#
#   Hi3516CV610_<tag>.tgz           SDK tree (MPP, board support, samples) with
#                                   the U-Boot source packed inside it
#   linux-5.10.y-<tag>.tgz          full vendor kernel source
#   <toolchain>-codesize.tgz        cross toolchain shipped in the repository
#
# The repository is cloned to vendor/upstream/ on first use (about 2.5 GB
# with the kernel and U-Boot submodules) and reused afterwards.
set -euo pipefail

TOPDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TAG=${1:-tag_V2.1.0_Beta}
REPO_URL=https://gitcode.com/HiSpark/Hi3516CV610
VENDOR_DIR="${TOPDIR}/vendor"
SRC_DIR="${VENDOR_DIR}/upstream/Hi3516CV610-${TAG}"
TOOLCHAIN_PKG=gcc-10.3-arm-musl-x86-linux-26.06.1-codesize.tgz
LINUX_SUBDIR=third_party/linux/linux-5.10.y
UBOOT_SUBDIR=third_party/u-boot/u-boot-2022.07

SDK_TGZ="${VENDOR_DIR}/Hi3516CV610_${TAG}.tgz"
LINUX_TGZ="${VENDOR_DIR}/linux-5.10.y-${TAG}.tgz"
TOOLCHAIN_TGZ="${VENDOR_DIR}/${TOOLCHAIN_PKG}"

mkdir -p "${VENDOR_DIR}/upstream"

if [[ ! -d "${SRC_DIR}/.git" ]]; then
    git clone --branch "${TAG}" --depth 1 "${REPO_URL}" "${SRC_DIR}"
fi
for sub in "${LINUX_SUBDIR}" "${UBOOT_SUBDIR}"; do
    if [[ ! -e "${SRC_DIR}/${sub}/Makefile" ]]; then
        git -C "${SRC_DIR}" submodule update --init --depth 1 "${sub}"
    fi
done

if [[ ! -f "${TOOLCHAIN_TGZ}" ]]; then
    cp "${SRC_DIR}/prebuilts/${TOOLCHAIN_PKG}" "${TOOLCHAIN_TGZ}.tmp"
    mv "${TOOLCHAIN_TGZ}.tmp" "${TOOLCHAIN_TGZ}"
fi

if [[ ! -f "${LINUX_TGZ}" ]]; then
    git -C "${SRC_DIR}/${LINUX_SUBDIR}" archive --format=tar.gz \
        --prefix=linux-5.10.y/ -o "${LINUX_TGZ}.tmp" HEAD
    mv "${LINUX_TGZ}.tmp" "${LINUX_TGZ}"
fi

if [[ ! -f "${SDK_TGZ}" ]]; then
    work=$(mktemp -d "${VENDOR_DIR}/.pack.XXXXXX")
    trap 'rm -rf "${work}"' EXIT
    git -C "${SRC_DIR}/${UBOOT_SUBDIR}" archive --format=tar.gz \
        --prefix=u-boot-2022.07/ -o "${work}/u-boot-2022.07.tgz" HEAD
    # The SDK tree without what the build does not use (documentation, the
    # toolchain archive, glibc libraries) and without the kernel and U-Boot
    # work trees, which travel as source archives.
    tar -C "${SRC_DIR}" -cf "${work}/sdk.tar" \
        --transform 'flags=r;s,^\.,Hi3516CV610,' \
        --exclude=./.git --exclude='.git' --exclude=./docs --exclude=./prebuilts \
        --exclude=./soc/libs/glibc \
        --exclude="./${LINUX_SUBDIR}" --exclude="./${UBOOT_SUBDIR}" \
        .
    tar -C "${work}" -rf "${work}/sdk.tar" \
        --transform 'flags=r;s,^,Hi3516CV610/third_party/u-boot/,' \
        u-boot-2022.07.tgz
    gzip -c "${work}/sdk.tar" > "${SDK_TGZ}.tmp"
    mv "${SDK_TGZ}.tmp" "${SDK_TGZ}"
fi

ls -l "${SDK_TGZ}" "${LINUX_TGZ}" "${TOOLCHAIN_TGZ}"
