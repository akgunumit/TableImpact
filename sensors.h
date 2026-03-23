// sensors.h — Sensor interface for MacBook accelerometer + lid angle
// Header-only: #include "sensors.h" and compile with:
//   -framework IOKit -framework CoreFoundation -lm

#ifndef SENSORS_H
#define SENSORS_H

#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

// ── config ────────────────────────────────────────────────────────

#define IMPACT_THRESHOLD  0.05
#define IMPACT_COOLDOWN   0.28
#define CAL_SECONDS       2.0
#define ACCEL_RATE        100.0
#define REPORT_SIZE       64

// ── helpers ───────────────────────────────────────────────────────

static int prop_int(io_service_t svc, CFStringRef key) {
    CFTypeRef ref = IORegistryEntryCreateCFProperty(svc, key, kCFAllocatorDefault, 0);
    if (!ref) return -1;
    int val = -1;
    if (CFGetTypeID(ref) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &val);
    CFRelease(ref);
    return val;
}

static double now_sec(void) {
    return CFAbsoluteTimeGetCurrent();
}

// ── accelerometer state ───────────────────────────────────────────

static struct {
    IOHIDDeviceRef dev;
    uint8_t        report_buf[REPORT_SIZE];
    int            active;

    double cal_sum_x, cal_sum_y, cal_sum_z;
    int    cal_count;
    int    cal_target;
    int    cal_done;
    double base_x, base_y, base_z;

    double x, y, z;
    double mag;

    int    impact_count;
    double last_impact_time;
    double last_impact_mag;
} g_accel = {0};

static int g_impact_last_consumed = 0;

// Returns 1 if a new impact occurred since last call, 0 otherwise.
static int consume_impact(void) {
    if (g_accel.impact_count > g_impact_last_consumed) {
        g_impact_last_consumed = g_accel.impact_count;
        return 1;
    }
    return 0;
}

// Returns the magnitude of the last impact (>0 if new impact), 0.0 if none.
static double consume_impact_force(void) {
    if (g_accel.impact_count > g_impact_last_consumed) {
        g_impact_last_consumed = g_accel.impact_count;
        return g_accel.last_impact_mag;
    }
    return 0.0;
}

static void accel_report_cb(void *ctx, IOReturn result, void *sender,
                             IOHIDReportType type, uint32_t reportID,
                             uint8_t *report, CFIndex length) {
    (void)ctx; (void)result; (void)sender; (void)type; (void)reportID;
    if (length < 18) return;

    int32_t rx = (int32_t)(report[6]  | (report[7]  << 8) |
                           (report[8]  << 16) | (report[9]  << 24));
    int32_t ry = (int32_t)(report[10] | (report[11] << 8) |
                           (report[12] << 16) | (report[13] << 24));
    int32_t rz = (int32_t)(report[14] | (report[15] << 8) |
                           (report[16] << 16) | (report[17] << 24));

    double ax = rx / 65536.0;
    double ay = ry / 65536.0;
    double az = rz / 65536.0;

    g_accel.active = 1;

    if (!g_accel.cal_done) {
        g_accel.cal_sum_x += ax;
        g_accel.cal_sum_y += ay;
        g_accel.cal_sum_z += az;
        g_accel.cal_count++;
        if (g_accel.cal_count >= g_accel.cal_target) {
            g_accel.base_x = g_accel.cal_sum_x / g_accel.cal_count;
            g_accel.base_y = g_accel.cal_sum_y / g_accel.cal_count;
            g_accel.base_z = g_accel.cal_sum_z / g_accel.cal_count;
            g_accel.cal_done = 1;
        }
        return;
    }

    g_accel.x = ax;
    g_accel.y = ay;
    g_accel.z = az;

    double dx = ax - g_accel.base_x;
    double dy = ay - g_accel.base_y;
    double dz = az - g_accel.base_z;
    double mag = sqrt(dx*dx + dy*dy + dz*dz);
    g_accel.mag = mag;

    double t = now_sec();
    if (mag > IMPACT_THRESHOLD &&
        (t - g_accel.last_impact_time) > IMPACT_COOLDOWN) {
        g_accel.impact_count++;
        g_accel.last_impact_time = t;
        g_accel.last_impact_mag = mag;
    }
}

// Wake SPU drivers — powers on the accelerometer hardware.
// Without this, the sensor is dormant after a fresh boot until
// something (like `spank`) pokes these properties.
static void wake_spu_drivers(void) {
    CFMutableDictionaryRef match = IOServiceMatching("AppleSPUHIDDriver");
    if (!match) return;
    io_iterator_t iter = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS || !iter)
        return;
    io_service_t svc;
    while ((svc = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        int32_t one = 1;
        int32_t interval = 1000; // microseconds → ~1kHz reports
        CFNumberRef cfOne      = CFNumberCreate(NULL, kCFNumberSInt32Type, &one);
        CFNumberRef cfInterval = CFNumberCreate(NULL, kCFNumberSInt32Type, &interval);
        IORegistryEntrySetCFProperty(svc, CFSTR("SensorPropertyReportingState"), cfOne);
        IORegistryEntrySetCFProperty(svc, CFSTR("SensorPropertyPowerState"),     cfOne);
        IORegistryEntrySetCFProperty(svc, CFSTR("ReportInterval"),               cfInterval);
        CFRelease(cfOne);
        CFRelease(cfInterval);
        IOObjectRelease(svc);
    }
    IOObjectRelease(iter);
}

static int setup_accel(void) {
    wake_spu_drivers();

    CFMutableDictionaryRef match = IOServiceMatching("AppleSPUHIDDevice");
    if (!match)
        match = IOServiceMatching("AppleSPUHIDInterface");

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS || !iter) {
        fprintf(stderr, "No AppleSPUHIDDevice found\n");
        return 0;
    }

    io_service_t svc;
    IOHIDDeviceRef found = NULL;

    while ((svc = IOIteratorNext(iter)) != 0) {
        int page  = prop_int(svc, CFSTR("PrimaryUsagePage"));
        int usage = prop_int(svc, CFSTR("PrimaryUsage"));

        if (page == 0xFF00 && usage == 3) {
            IOHIDDeviceRef dev = IOHIDDeviceCreate(kCFAllocatorDefault, svc);
            if (dev) {
                IOReturn ret = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeSeizeDevice);
                if (ret != kIOReturnSuccess)
                    ret = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
                if (ret == kIOReturnSuccess) {
                    found = dev;
                    IOObjectRelease(svc);
                    break;
                } else {
                    CFRelease(dev);
                }
            }
        }
        IOObjectRelease(svc);
    }
    IOObjectRelease(iter);

    if (!found) {
        fprintf(stderr, "Could not open SPU accelerometer\n");
        return 0;
    }

    g_accel.dev = found;
    g_accel.cal_target = (int)(CAL_SECONDS * ACCEL_RATE);

    IOHIDDeviceRegisterInputReportCallback(
        found, g_accel.report_buf, REPORT_SIZE, accel_report_cb, NULL);
    IOHIDDeviceScheduleWithRunLoop(found, CFRunLoopGetCurrent(),
                                    kCFRunLoopDefaultMode);
    return 1;
}

static void close_accel(void) {
    if (!g_accel.dev) return;
    IOHIDDeviceUnscheduleFromRunLoop(g_accel.dev, CFRunLoopGetCurrent(),
                                      kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(g_accel.dev, g_accel.report_buf,
                                            REPORT_SIZE, NULL, NULL);
    IOHIDDeviceClose(g_accel.dev, kIOHIDOptionsTypeNone);
    CFRelease(g_accel.dev);
    g_accel.dev = NULL;
    g_accel.active = 0;
    // preserve cal_done, base_x/y/z so recalibration is skipped on reopen
}

// ── lid sensor ────────────────────────────────────────────────────

static struct {
    int            angle;
    int            orient;
    int            valid;
    IOHIDDeviceRef dev;
    CFArrayRef     elements;
} g_lid = {0};

static IOHIDManagerRef g_lid_mgr = NULL;

static void poll_lid(void) {
    if (!g_lid.dev || !g_lid.elements) return;
    for (CFIndex i = 0; i < CFArrayGetCount(g_lid.elements); i++) {
        IOHIDElementRef el = (IOHIDElementRef)CFArrayGetValueAtIndex(g_lid.elements, i);
        if (IOHIDElementGetType(el) == kIOHIDElementTypeCollection) continue;
        IOHIDValueRef val = NULL;
        if (IOHIDDeviceGetValue(g_lid.dev, el, &val) != kIOReturnSuccess) continue;
        uint32_t u = IOHIDElementGetUsage(el);
        CFIndex  v = IOHIDValueGetIntegerValue(val);
        if (u == 0x047f) g_lid.angle  = (int)v;
        if (u == 0x0303) g_lid.orient = (int)v;
    }
    g_lid.valid = 1;
}

static void matched_lid(void *ctx, IOReturn result, void *sender,
                         IOHIDDeviceRef dev) {
    (void)ctx; (void)result; (void)sender;
    if (g_lid.dev) return;
    if (IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone) != kIOReturnSuccess) return;
    g_lid.dev      = dev;
    g_lid.elements = IOHIDDeviceCopyMatchingElements(dev, NULL, kIOHIDOptionsTypeNone);
}

static void setup_lid(void) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    g_lid_mgr = mgr;
    int page = 0x0020, usage = 0x008a;
    CFMutableDictionaryRef m = CFDictionaryCreateMutable(kCFAllocatorDefault, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNumberRef pn = CFNumberCreate(NULL, kCFNumberIntType, &page);
    CFNumberRef un = CFNumberCreate(NULL, kCFNumberIntType, &usage);
    CFDictionarySetValue(m, CFSTR(kIOHIDDeviceUsagePageKey), pn);
    CFDictionarySetValue(m, CFSTR(kIOHIDDeviceUsageKey), un);
    IOHIDManagerSetDeviceMatching(mgr, m);
    IOHIDManagerRegisterDeviceMatchingCallback(mgr, matched_lid, NULL);
    IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    CFRelease(pn);
    CFRelease(un);
    CFRelease(m);
}

static void close_lid(void) {
    if (g_lid_mgr) {
        IOHIDManagerUnscheduleFromRunLoop(g_lid_mgr, CFRunLoopGetCurrent(),
                                           kCFRunLoopDefaultMode);
        IOHIDManagerClose(g_lid_mgr, kIOHIDOptionsTypeNone);
        CFRelease(g_lid_mgr);
        g_lid_mgr = NULL;
    }
    if (g_lid.elements) {
        CFRelease(g_lid.elements);
        g_lid.elements = NULL;
    }
    if (g_lid.dev) {
        IOHIDDeviceClose(g_lid.dev, kIOHIDOptionsTypeNone);
        g_lid.dev = NULL;
    }
    g_lid.valid = 0;
}

#endif // SENSORS_H
