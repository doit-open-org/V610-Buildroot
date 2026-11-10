#!/usr/bin/env bash
# Assemble the flashable image set in ${BINARIES_DIR}/flash:
#   boot_image.bin  GSL + U-Boot           (hisi-boot)
#   nand_env.bin    U-Boot environment
#   uImage          FIT: zImage + one dtb per peripheral mode (lcd, sd, none)
#   rootfs_*.ubifs  UBI image holding the UBIFS root
#   nand_burn_table.xml  partition table for the HiSilicon burn tool
set -euo pipefail

BOARD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=flash.cfg
. "${BOARD_DIR}/flash.cfg"

DTB_LCD="${BINARIES_DIR}/hi3516cv610-demb-flash.dtb"
DTB_SD="${BINARIES_DIR}/hi3516cv610-demb-flash-sd.dtb"
DTB_NONE="${BINARIES_DIR}/hi3516cv610-demb-flash-none.dtb"
FLASH_DIR="${BINARIES_DIR}/flash"
WORK_DIR="${BUILD_DIR}/cove_610-fit"

human() { # bytes -> 512K / 59M, as used in mtdparts
    if (( $1 % 1048576 == 0 )); then echo "$(( $1 / 1048576 ))M"; else echo "$(( $1 / 1024 ))K"; fi
}

check_size() { # file limit partition
    local size
    size=$(stat -c %s "$1")
    if (( size > $2 )); then
        echo "post-image: $(basename "$1") is ${size} bytes, larger than the $3 partition ($2 bytes)" >&2
        exit 1
    fi
}

for f in "${BINARIES_DIR}/zImage" "${DTB_LCD}" "${DTB_SD}" "${DTB_NONE}" "${BINARIES_DIR}/rootfs.ubi"; do
    [[ -f "$f" ]] || { echo "post-image: missing $f" >&2; exit 1; }
done

rm -rf "${FLASH_DIR}" "${WORK_DIR}"
mkdir -p "${FLASH_DIR}" "${WORK_DIR}"

# FIT image, same layout as the vendor BSP (boards/dmeb fit_assemble).
cp "${BINARIES_DIR}/zImage" "${WORK_DIR}/zImage"
cp "${DTB_LCD}" "${WORK_DIR}/devicetree-lcd.dtb"
cp "${DTB_SD}" "${WORK_DIR}/devicetree-sd.dtb"
cp "${DTB_NONE}" "${WORK_DIR}/devicetree-none.dtb"
cp "${BOARD_DIR}/linux_image.its" "${WORK_DIR}/linux_image.its"
(cd "${WORK_DIR}" && "${HOST_DIR}/bin/mkimage" -f linux_image.its uImage >/dev/null)
cp "${WORK_DIR}/uImage" "${FLASH_DIR}/uImage"
check_size "${FLASH_DIR}/uImage" "${KERNEL_SIZE}" kernel

# U-Boot environment; mtdparts follows flash.cfg. sdio1_pins selects the
# FIT configuration through boot_lcd / boot_sd / boot_none. Should it name none of them, run
# fails without starting bootm and the plain bootm boots the default
# configuration; a failed bootm#conf would leave bootm unable to retry.
mtdparts="mtdparts=nand:$(human "${BOOT_SIZE}")(boot),$(human "${ENV_SIZE}")(env),$(human "${KERNEL_SIZE}")(kernel),$(human "${ROOTFS_SIZE}")(rootfs)"
kernel_off=$(( BOOT_SIZE + ENV_SIZE ))
sed -e "s|mtdparts=nand:[^ ]*|${mtdparts}|" \
    -e "s|^bootcmd=.*|bootcmd=nand read 0x41000000 $(printf '0x%x' "${kernel_off}") $(printf '0x%x' "${KERNEL_SIZE}"); run boot_\${sdio1_pins}; bootm 0x41000000|" \
    "${BOARD_DIR}/nand_env.txt" > "${WORK_DIR}/nand_env.txt"
"${HOST_DIR}/bin/mkenvimage" -s "${ENV_IMAGE_SIZE}" -o "${FLASH_DIR}/nand_env.bin" "${WORK_DIR}/nand_env.txt"
cp "${WORK_DIR}/nand_env.txt" "${FLASH_DIR}/nand_env.txt"

# The vendor tools call the UBI image "<...>.ubifs".
rootfs_img="rootfs_${CHIP}_${NAND_PAGE}_${NAND_BLOCK}_$(human "${ROOTFS_SIZE}").ubifs"
cp "${BINARIES_DIR}/rootfs.ubi" "${FLASH_DIR}/${rootfs_img}"
check_size "${FLASH_DIR}/${rootfs_img}" "${ROOTFS_SIZE}" rootfs

boot_img=""
if [[ -f "${BINARIES_DIR}/boot_image.bin" ]]; then
    cp "${BINARIES_DIR}/boot_image.bin" "${FLASH_DIR}/boot_image.bin"
    check_size "${FLASH_DIR}/boot_image.bin" "${BOOT_SIZE}" boot
    boot_img="boot_image.bin"
fi

part() { # name start length file
    printf '<Part Sel="%s" PartitionName="%s" FlashType="nand" FileSystem="none" Start="0x%x" Length="0x%x" SelectFile="%s" />' \
        "$([[ -n "$4" ]] && echo 1 || echo 0)" "$1" "$2" "$3" "$4"
}
{
    printf '<Partition_Info>'
    part boot 0 "${BOOT_SIZE}" "${boot_img}"
    part env "${BOOT_SIZE}" "${ENV_SIZE}" nand_env.bin
    part kernel "${kernel_off}" "${KERNEL_SIZE}" uImage
    part rootfs "$(( kernel_off + KERNEL_SIZE ))" "${ROOTFS_SIZE}" "${rootfs_img}"
    printf '</Partition_Info>'
} > "${FLASH_DIR}/nand_burn_table.xml"

echo "post-image: flash images are in ${FLASH_DIR}"
ls -l "${FLASH_DIR}"
