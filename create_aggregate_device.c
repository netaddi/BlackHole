/*
 * create_aggregate_device.c
 * Creates a macOS aggregate audio device combining all BlackHole_* devices.
 *
 * Compile: clang -o create_aggregate_device create_aggregate_device.c \
 *          -framework CoreAudio -framework CoreFoundation
 * Usage:   ./create_aggregate_device [aggregate_name] [aggregate_uid]
 */

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

// Get the UID string for a given audio device
static CFStringRef get_device_uid(AudioObjectID deviceID) {
    CFStringRef uid = NULL;
    UInt32 size = sizeof(uid);
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    OSStatus status = AudioObjectGetPropertyData(deviceID, &addr, 0, NULL, &size, &uid);
    if (status != noErr) return NULL;
    return uid;
}

// Get the name string for a given audio device
static CFStringRef get_device_name(AudioObjectID deviceID) {
    CFStringRef name = NULL;
    UInt32 size = sizeof(name);
    AudioObjectPropertyAddress addr = {
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    OSStatus status = AudioObjectGetPropertyData(deviceID, &addr, 0, NULL, &size, &name);
    if (status != noErr) return NULL;
    return name;
}

// Check if a device name starts with "BlackHole_"
static Boolean is_blackhole_device(AudioObjectID deviceID) {
    CFStringRef name = get_device_name(deviceID);
    if (name == NULL) return false;
    Boolean result = CFStringHasPrefix(name, CFSTR("BlackHole_"));
    CFRelease(name);
    return result;
}

// Remove existing aggregate device with a given UID if it exists
static void remove_existing_aggregate(const char* uid) {
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size);
    if (status != noErr) return;

    UInt32 deviceCount = size / sizeof(AudioObjectID);
    AudioObjectID* devices = malloc(size);
    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, devices);
    if (status != noErr) { free(devices); return; }

    CFStringRef targetUID = CFStringCreateWithCString(NULL, uid, kCFStringEncodingUTF8);

    for (UInt32 i = 0; i < deviceCount; i++) {
        CFStringRef deviceUID = get_device_uid(devices[i]);
        if (deviceUID && CFStringCompare(deviceUID, targetUID, 0) == kCFCompareEqualTo) {
            printf("Removing existing aggregate device: %s\n", uid);
            AudioHardwareDestroyAggregateDevice(devices[i]);
            CFRelease(deviceUID);
            break;
        }
        if (deviceUID) CFRelease(deviceUID);
    }

    CFRelease(targetUID);
    free(devices);
}

int main(int argc, const char* argv[]) {
    const char* aggregateName = "BlackHole_Aggregate";
    const char* aggregateUID  = "audio.existential.BlackHole_Aggregate_UID";

    if (argc >= 2) aggregateName = argv[1];
    if (argc >= 3) aggregateUID  = argv[2];

    // Remove any existing aggregate with the same UID
    remove_existing_aggregate(aggregateUID);

    // Get all audio devices
    AudioObjectPropertyAddress devicesAddr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devicesAddr, 0, NULL, &size);
    if (status != noErr) {
        fprintf(stderr, "Error: Cannot get audio device list (status=%d)\n", status);
        return 1;
    }

    UInt32 deviceCount = size / sizeof(AudioObjectID);
    AudioObjectID* devices = malloc(size);
    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &devicesAddr, 0, NULL, &size, devices);
    if (status != noErr) {
        fprintf(stderr, "Error: Cannot enumerate audio devices (status=%d)\n", status);
        free(devices);
        return 1;
    }

    // Collect UIDs of all BlackHole_* devices
    CFMutableArrayRef subDeviceList = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    int found = 0;

    printf("Scanning for BlackHole_* devices...\n");
    for (UInt32 i = 0; i < deviceCount; i++) {
        if (is_blackhole_device(devices[i])) {
            CFStringRef uid = get_device_uid(devices[i]);
            CFStringRef name = get_device_name(devices[i]);
            if (uid && name) {
                char nameBuf[256], uidBuf[256];
                CFStringGetCString(name, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8);
                CFStringGetCString(uid, uidBuf, sizeof(uidBuf), kCFStringEncodingUTF8);
                printf("  Found: %s (UID: %s)\n", nameBuf, uidBuf);
                CFArrayAppendValue(subDeviceList, uid);
                found++;
            }
            if (uid) CFRelease(uid);
            if (name) CFRelease(name);
        }
    }
    free(devices);

    if (found == 0) {
        fprintf(stderr, "Error: No BlackHole_* devices found. Install them first.\n");
        CFRelease(subDeviceList);
        return 1;
    }

    printf("\nCreating aggregate device \"%s\" with %d sub-devices...\n", aggregateName, found);

    // Build the aggregate device description dictionary
    CFMutableDictionaryRef aggDesc = CFDictionaryCreateMutable(
        NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFStringRef cfName = CFStringCreateWithCString(NULL, aggregateName, kCFStringEncodingUTF8);
    CFStringRef cfUID  = CFStringCreateWithCString(NULL, aggregateUID,  kCFStringEncodingUTF8);

    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceNameKey), cfName);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceUIDKey), cfUID);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subDeviceList);

    // Make it public (visible in Audio MIDI Setup) and not stacked
    int zero = 0;
    CFNumberRef cfZero = CFNumberCreate(NULL, kCFNumberIntType, &zero);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceIsPrivateKey), cfZero);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceIsStackedKey), cfZero);

    // Create the aggregate device
    AudioObjectID aggDeviceID = 0;
    status = AudioHardwareCreateAggregateDevice(aggDesc, &aggDeviceID);

    CFRelease(cfName);
    CFRelease(cfUID);
    CFRelease(cfZero);
    CFRelease(subDeviceList);
    CFRelease(aggDesc);

    if (status != noErr) {
        fprintf(stderr, "Error: Failed to create aggregate device (status=%d)\n", status);
        return 1;
    }

    printf("✅ Aggregate device \"%s\" created successfully! (ID: %u)\n", aggregateName, aggDeviceID);
    return 0;
}
