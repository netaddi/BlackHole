#!/usr/bin/env bash
set -euo pipefail

# Uninstall all BlackHole virtual audio cables defined in cables.txt.
# Run from the BlackHole repo root:
#   ./uninstall_all.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/cables.txt"
INSTALL_DIR="/Library/Audio/Plug-Ins/HAL"

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

echo "Uninstalling ${#cables[@]} BlackHole cable(s)..."

# Remove associated aggregate device first
AGGREGATE_SRC="$SCRIPT_DIR/create_aggregate_device.c"
AGGREGATE_BIN="/tmp/remove_aggregate_device_$$"
if [ -f "$AGGREGATE_SRC" ]; then
    cat > /tmp/remove_aggregate_$$.c << 'REMOVECODE'
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
int main(int argc, const char* argv[]) {
    const char* uid = (argc >= 2) ? argv[1] : "audio.existential.BlackHole_Aggregate_UID";
    AudioObjectPropertyAddress addr = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size);
    UInt32 count = size / sizeof(AudioObjectID);
    AudioObjectID* devs = malloc(size);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, devs);
    CFStringRef target = CFStringCreateWithCString(NULL, uid, kCFStringEncodingUTF8);
    for (UInt32 i = 0; i < count; i++) {
        CFStringRef duid = NULL; UInt32 s = sizeof(duid);
        AudioObjectPropertyAddress a2 = { kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        AudioObjectGetPropertyData(devs[i], &a2, 0, NULL, &s, &duid);
        if (duid && CFStringCompare(duid, target, 0) == kCFCompareEqualTo) {
            AudioHardwareDestroyAggregateDevice(devs[i]);
            printf("Removed aggregate device.\n");
            CFRelease(duid); break;
        }
        if (duid) CFRelease(duid);
    }
    CFRelease(target); free(devs);
    return 0;
}
REMOVECODE
    clang -o "$AGGREGATE_BIN" /tmp/remove_aggregate_$$.c -framework CoreAudio -framework CoreFoundation 2>/dev/null
    "$AGGREGATE_BIN" "audio.existential.BlackHole_Aggregate_UID" 2>/dev/null || true
    rm -f "$AGGREGATE_BIN" /tmp/remove_aggregate_$$.c
fi

removed=0
for suffix in "${cables[@]}"; do
    driverBundleName="BlackHole_${suffix}.driver"
    driverPath="${INSTALL_DIR}/${driverBundleName}"
    if [ -d "$driverPath" ]; then
        sudo rm -rf "$driverPath"
        echo "  ✅ Removed: ${driverBundleName}"
        ((removed++))
    else
        echo "  ⏭  Not found: ${driverBundleName} (skipped)"
    fi
done

if [ $removed -gt 0 ]; then
    echo ""
    echo "Restarting coreaudiod..."
    sudo killall coreaudiod || true
fi

echo ""
echo "Done. Removed $removed cable(s)."
