#!/usr/bin/env bash
# Final adjustments to the target tree before the images are built.
set -euo pipefail

TARGET_DIR="$1"

# bluetoothd is started on demand by ble-service once S51wireless has
# brought up hci0, not at boot.
rm -f "${TARGET_DIR}/etc/init.d/S40bluetoothd"

# The vendor kernel configuration builds a set of DVB tuner and demodulator
# modules that no Cove-610 hardware uses.
rm -rf "${TARGET_DIR}"/lib/modules/*/kernel/drivers/media

# Include package ownership metadata in exported Buildroot SDKs. The application
# SDK uses it to keep target development files grouped by their source package.
if [[ -f "${BUILD_DIR}/packages-file-list-staging.txt" ]]; then
    install -D -m 0644 "${BUILD_DIR}/packages-file-list-staging.txt" \
        "${HOST_DIR}/share/v610-platform/packages-file-list-staging.txt"
fi
