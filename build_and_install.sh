#!/usr/bin/env bash
set -euo pipefail

# Build and install multiple BlackHole virtual audio cables.
# Reads cable suffixes from cables.txt.
# Uses clang directly (no Xcode.app required, only Command Line Tools).
# Run from the BlackHole repo root:
#   ./build_and_install.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/cables.txt"
INSTALL_DIR="/Library/Audio/Plug-Ins/HAL"
SOURCE_FILE="$SCRIPT_DIR/BlackHole/BlackHole.c"
PLIST_TEMPLATE="$SCRIPT_DIR/BlackHole/BlackHole.plist"
VERSION=$(cat "$SCRIPT_DIR/VERSION" 2>/dev/null || echo "0.6.1")

# Validation
if [ ! -f "$SOURCE_FILE" ]; then
    echo "Error: BlackHole.c not found at $SOURCE_FILE"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: cables.txt not found at $CONFIG_FILE"
    exit 1
fi

# Read cable suffixes (skip comments and blank lines)
cables=()
while IFS= read -r line || [ -n "$line" ]; do
    line="$(echo "$line" | xargs)"
    [[ -z "$line" || "$line" == \#* ]] && continue
    cables+=("$line")
done < "$CONFIG_FILE"

if [ ${#cables[@]} -eq 0 ]; then
    echo "Error: No cable suffixes found in cables.txt"
    exit 1
fi

echo "=========================================="
echo "BlackHole Multi-Cable Builder"
echo "=========================================="
echo "Found ${#cables[@]} cable(s): ${cables[*]}"
echo ""

# Uninstall all existing BlackHole drivers first
echo "Removing existing BlackHole drivers..."
existing_drivers=("${INSTALL_DIR}"/BlackHole*.driver)
if [ -e "${existing_drivers[0]}" ]; then
    for drv in "${existing_drivers[@]}"; do
        sudo rm -rf "$drv"
        echo "  Removed: $(basename "$drv")"
    done
    echo "Restarting coreaudiod after cleanup..."
    sudo killall coreaudiod || true
    sleep 2
else
    echo "  No existing BlackHole drivers found."
fi
echo ""

# Clean previous build artifacts
rm -rf build

for suffix in "${cables[@]}"; do
    driverName="BlackHole_${suffix}"
    bundleID="audio.existential.${driverName}"
    channels=2

    echo "------------------------------------------"
    echo "Building: $driverName (${channels}ch)"
    echo "  Bundle ID: $bundleID"
    echo "------------------------------------------"

    # Create bundle directory structure
    bundleDir="build/${driverName}.driver"
    mkdir -p "${bundleDir}/Contents/MacOS"

    # Compile with clang
    clang \
        -bundle \
        -o "${bundleDir}/Contents/MacOS/${driverName}" \
        -DkNumber_Of_Channels=${channels} \
        -DkPlugIn_BundleID="\"${bundleID}\"" \
        -DkDriver_Name="\"${driverName}\"" \
        -DkHas_Driver_Name_Format=false \
        -framework CoreAudio \
        -framework CoreFoundation \
        -framework Accelerate \
        -arch x86_64 -arch arm64 \
        -mmacosx-version-min=10.10 \
        -Os \
        "$SOURCE_FILE"

    # Generate Info.plist with a new UUID
    uuid=$(uuidgen)
    sed "s/e395c745-4eea-4d94-bb92-46224221047c/${uuid}/g; \
         s/\${EXECUTABLE_NAME}/${driverName}/g; \
         s/\$(PRODUCT_BUNDLE_IDENTIFIER)/${bundleID}/g; \
         s/\${PRODUCT_NAME}/${driverName}/g; \
         s/\$(MARKETING_VERSION)/${VERSION}/g" \
        "$PLIST_TEMPLATE" > "${bundleDir}/Contents/Info.plist"

    # Install to HAL directory
    echo "Installing ${driverName}.driver to ${INSTALL_DIR}/"
    sudo mkdir -p "$INSTALL_DIR"
    sudo rm -rf "${INSTALL_DIR}/${driverName}.driver"
    sudo cp -R "${bundleDir}" "${INSTALL_DIR}/${driverName}.driver"
    sudo chown -R root:wheel "${INSTALL_DIR}/${driverName}.driver"

    echo "✅ Installed: ${driverName}.driver"
    echo ""
done

# Clean up build directory
rm -rf build

# Restart coreaudiod to pick up new drivers
echo "Restarting coreaudiod..."
sudo killall coreaudiod || true
sleep 3

# Create aggregate device combining all BlackHole cables
echo ""
echo "Creating aggregate audio device..."
AGGREGATE_SRC="$SCRIPT_DIR/create_aggregate_device.c"
AGGREGATE_BIN="/tmp/create_aggregate_device_$$"

if [ -f "$AGGREGATE_SRC" ]; then
    clang -o "$AGGREGATE_BIN" "$AGGREGATE_SRC" \
        -framework CoreAudio -framework CoreFoundation \
        -arch x86_64 -arch arm64 2>/dev/null

    "$AGGREGATE_BIN" "BlackHole_Aggregate" "audio.existential.BlackHole_Aggregate_UID"
    rm -f "$AGGREGATE_BIN"
else
    echo "Warning: create_aggregate_device.c not found, skipping aggregate device creation."
fi

echo ""
echo "=========================================="
echo "✅ All ${#cables[@]} cable(s) installed successfully!"
echo "=========================================="
echo "Installed cables:"
for suffix in "${cables[@]}"; do
    echo "  - BlackHole_${suffix}"
done
echo ""
echo "Open Audio MIDI Setup or System Settings → Sound to verify."
