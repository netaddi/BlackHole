/*
 * create_aggregate_device.c
 *
 * Creates a macOS aggregate audio device combining:
 *   - The system default OUTPUT device (e.g. Zen Go) as clock master + output
 *   - All BlackHole_* devices as input sub-devices
 *
 * This matches the standard "Audio MIDI Setup → Create Aggregate Device" workflow:
 * one aggregate for BOTH input and output in Logic, with a hardware clock source.
 *
 * Compile:
 *   clang -o create_aggregate_device create_aggregate_device.c \
 *         -framework CoreAudio -framework CoreFoundation
 * Usage:
 *   ./create_aggregate_device [aggregate_name] [aggregate_uid]
 */

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static CFStringRef get_string_prop(AudioObjectID obj, AudioObjectPropertySelector sel) {
    CFStringRef val = NULL;
    UInt32 size = sizeof(val);
    AudioObjectPropertyAddress addr = { sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectGetPropertyData(obj, &addr, 0, NULL, &size, &val);
    return val;  // caller must CFRelease
}

static OSStatus set_float64_prop(AudioObjectID obj, AudioObjectPropertySelector sel, Float64 val) {
    AudioObjectPropertyAddress addr = { sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    return AudioObjectSetPropertyData(obj, &addr, 0, NULL, sizeof(val), &val);
}

static Float64 get_float64_prop(AudioObjectID obj, AudioObjectPropertySelector sel) {
    Float64 val = 0;
    UInt32 size = sizeof(val);
    AudioObjectPropertyAddress addr = { sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectGetPropertyData(obj, &addr, 0, NULL, &size, &val);
    return val;
}

static UInt32 get_channel_count(AudioObjectID deviceID, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress addr = { kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(deviceID, &addr, 0, NULL, &size);
    AudioBufferList* list = malloc(size);
    UInt32 count = 0;
    if (AudioObjectGetPropertyData(deviceID, &addr, 0, NULL, &size, list) == noErr) {
        for (UInt32 i = 0; i < list->mNumberBuffers; i++)
            count += list->mBuffers[i].mNumberChannels;
    }
    free(list);
    return count;
}

// Remove existing aggregate with a given UID
static void remove_existing_aggregate(const char* uid) {
    AudioObjectPropertyAddress addr = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size) != noErr) return;
    UInt32 count = size / sizeof(AudioObjectID);
    AudioObjectID* devs = malloc(size);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, devs);

    CFStringRef target = CFStringCreateWithCString(NULL, uid, kCFStringEncodingUTF8);
    for (UInt32 i = 0; i < count; i++) {
        CFStringRef duid = get_string_prop(devs[i], kAudioDevicePropertyDeviceUID);
        if (duid && CFStringCompare(duid, target, 0) == kCFCompareEqualTo) {
            printf("Removing existing aggregate: %s\n", uid);
            AudioHardwareDestroyAggregateDevice(devs[i]);
            CFRelease(duid);
            break;
        }
        if (duid) CFRelease(duid);
    }
    CFRelease(target);
    free(devs);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, const char* argv[]) {
    const char* aggregateName = "BlackHole_Aggregate";
    const char* aggregateUID  = "audio.existential.BlackHole_Aggregate_UID";
    if (argc >= 2) aggregateName = argv[1];
    if (argc >= 3) aggregateUID  = argv[2];

    remove_existing_aggregate(aggregateUID);

    // Get all audio devices
    AudioObjectPropertyAddress devicesAddr = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devicesAddr, 0, NULL, &size);
    UInt32 totalCount = size / sizeof(AudioObjectID);
    AudioObjectID* devices = malloc(size);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &devicesAddr, 0, NULL, &size, devices);

    // Get the system default OUTPUT device (to use as clock master & output)
    AudioObjectID defaultOut = kAudioObjectUnknown;
    {
        AudioObjectPropertyAddress a = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        UInt32 s = sizeof(defaultOut);
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &s, &defaultOut);
    }
    Float64 systemRate = get_float64_prop(defaultOut, kAudioDevicePropertyNominalSampleRate);
    if (systemRate == 0) systemRate = 48000.0;

    CFStringRef defaultOutUID  = get_string_prop(defaultOut, kAudioDevicePropertyDeviceUID);
    CFStringRef defaultOutName = get_string_prop(defaultOut, kAudioObjectPropertyName);
    char outNameBuf[256] = "unknown";
    if (defaultOutName) CFStringGetCString(defaultOutName, outNameBuf, sizeof(outNameBuf), kCFStringEncodingUTF8);

    printf("System output device (clock master): %s @ %.0f Hz\n", outNameBuf, systemRate);

    // Build sub-device list:
    //   1) Output device (clock master, provides output channels)
    //   2) All BlackHole_* devices (provide input channels)
    CFMutableArrayRef subList = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    int bhCount = 0;

    // Add output device first (it becomes the clock master)
    if (defaultOutUID) {
        CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(d, CFSTR(kAudioSubDeviceUIDKey), defaultOutUID);
        CFArrayAppendValue(subList, d);
        CFRelease(d);
        CFRelease(defaultOutUID);
    }
    if (defaultOutName) CFRelease(defaultOutName);

    printf("Scanning for BlackHole_* devices...\n");
    for (UInt32 i = 0; i < totalCount; i++) {
        CFStringRef name = get_string_prop(devices[i], kAudioObjectPropertyName);
        if (!name) continue;
        Boolean isBH = CFStringHasPrefix(name, CFSTR("BlackHole_"));
        if (isBH) {
            CFStringRef uid = get_string_prop(devices[i], kAudioDevicePropertyDeviceUID);
            char nb[256] = "", ub[256] = "";
            CFStringGetCString(name, nb, sizeof(nb), kCFStringEncodingUTF8);
            if (uid) CFStringGetCString(uid, ub, sizeof(ub), kCFStringEncodingUTF8);
            UInt32 ins  = get_channel_count(devices[i], kAudioDevicePropertyScopeInput);
            UInt32 outs = get_channel_count(devices[i], kAudioDevicePropertyScopeOutput);
            printf("  + %s  (in:%u out:%u  UID: %s)\n", nb, ins, outs, ub);
            if (uid) {
                CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                CFDictionarySetValue(d, CFSTR(kAudioSubDeviceUIDKey), uid);
                CFArrayAppendValue(subList, d);
                CFRelease(d);
                CFRelease(uid);
            }
            bhCount++;
        }
        CFRelease(name);
    }
    free(devices);

    if (bhCount == 0) {
        fprintf(stderr, "Error: No BlackHole_* devices found.\n");
        CFRelease(subList);
        return 1;
    }

    printf("\nCreating aggregate \"%s\" with %d sub-device(s) @ %.0f Hz...\n",
           aggregateName, (int)CFArrayGetCount(subList), systemRate);

    // Build aggregate description
    CFMutableDictionaryRef desc = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFStringRef cfName = CFStringCreateWithCString(NULL, aggregateName, kCFStringEncodingUTF8);
    CFStringRef cfUID  = CFStringCreateWithCString(NULL, aggregateUID,  kCFStringEncodingUTF8);
    int zero = 0;
    CFNumberRef cfZero = CFNumberCreate(NULL, kCFNumberIntType, &zero);

    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceNameKey), cfName);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceUIDKey),  cfUID);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subList);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceIsPrivateKey), cfZero);  // visible
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceIsStackedKey), cfZero);  // parallel (not stacked)

    AudioObjectID aggID = 0;
    OSStatus status = AudioHardwareCreateAggregateDevice(desc, &aggID);

    CFRelease(cfName); CFRelease(cfUID); CFRelease(cfZero);
    CFRelease(subList); CFRelease(desc);

    if (status != noErr) {
        fprintf(stderr, "Error: Cannot create aggregate device (status=%d)\n", status);
        return 1;
    }

    // Set sample rate on aggregate to match system
    OSStatus rateStatus = set_float64_prop(aggID, kAudioDevicePropertyNominalSampleRate, systemRate);
    if (rateStatus != noErr)
        printf("Warning: could not set aggregate sample rate (status=%d)\n", rateStatus);
    else
        printf("Sample rate: %.0f Hz ✓\n", systemRate);

    // Print resulting channel layout
    UInt32 aggIns  = get_channel_count(aggID, kAudioDevicePropertyScopeInput);
    UInt32 aggOuts = get_channel_count(aggID, kAudioDevicePropertyScopeOutput);
    printf("✅ \"%s\" created (ID:%u)  — %u ins / %u outs\n", aggregateName, aggID, aggIns, aggOuts);
    printf("\nIn Logic Pro:\n");
    printf("  → Set BOTH Input AND Output device to \"%s\"\n", aggregateName);
    printf("  → Output goes to %s (channels 1-%u)\n", outNameBuf, aggOuts > 16 ? 16 : aggOuts);
    printf("  → BlackHole inputs start at channel %u\n", aggOuts + 1);
    return 0;
}
