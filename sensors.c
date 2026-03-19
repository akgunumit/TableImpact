// sensors.c — lid angle + SPU accelerometer for Apple Silicon MacBooks
//
// This reads the accelerometer the same way spank/olvvier do:
//   - Find AppleSPUHIDDevice in the IOKit registry
//   - Match PrimaryUsagePage=0xFF00, PrimaryUsage=3 (accel)
//   - Open with IOHIDDeviceCreate + IOHIDDeviceRegisterInputReportCallback
//   - Parse 22-byte HID reports: int32 LE at offsets 6,10,14 → divide by 65536
//
// Compile: clang -o sensors sensors.c -framework IOKit -framework CoreFoundation -lm
// Run:     sudo ./sensors

#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

// ── config ────────────────────────────────────────────────────────

#define IMPACT_THRESHOLD  0.05   // g-force above gravity baseline (ultra sensitive)
#define IMPACT_COOLDOWN   0.35   // seconds between alerts
#define CAL_SECONDS       2.0    // calibration duration
#define ACCEL_RATE        100.0  // expected ~100Hz callback rate
#define REPORT_SIZE       64     // buffer large enough for any report

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

    // calibration
    double cal_sum_x, cal_sum_y, cal_sum_z;
    int    cal_count;
    int    cal_target;
    int    cal_done;
    double base_x, base_y, base_z;

    // latest values
    double x, y, z;
    double mag;      // magnitude above baseline

    // impact detection
    int    impact_count;
    double last_impact_time;
} g_accel = {0};

static void accel_report_cb(void *ctx, IOReturn result, void *sender,
                             IOHIDReportType type, uint32_t reportID,
                             uint8_t *report, CFIndex length) {
    if (length < 18) return;   // need at least offsets 6..17

    // Parse x/y/z: int32 little-endian at byte offsets 6, 10, 14
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

    // Calibration phase
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
            fprintf(stderr, "✓ Accel calibrated (gravity: %.3f, %.3f, %.3f)\n",
                    g_accel.base_x, g_accel.base_y, g_accel.base_z);
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

    // Impact detection
    double t = now_sec();
    if (mag > IMPACT_THRESHOLD &&
        (t - g_accel.last_impact_time) > IMPACT_COOLDOWN) {
        g_accel.impact_count++;
        g_accel.last_impact_time = t;
        printf("IMPACT  force=%.2fg  count=%d  dx=%.3f dy=%.3f dz=%.3f\n",
               mag, g_accel.impact_count, dx, dy, dz);
        fflush(stdout);
    }
}

static int setup_accel(void) {
    // Iterate IOKit registry for AppleSPUHIDDevice with page=0xFF00 usage=3
    CFMutableDictionaryRef match = IOServiceMatching("AppleSPUHIDDevice");
    if (!match) {
        // Fallback: try the interface class name
        match = IOServiceMatching("AppleSPUHIDInterface");
    }

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS || !iter) {
        fprintf(stderr, "✗ No AppleSPUHIDDevice found in IOKit registry\n");
        return 0;
    }

    io_service_t svc;
    IOHIDDeviceRef found = NULL;

    while ((svc = IOIteratorNext(iter)) != 0) {
        int page  = prop_int(svc, CFSTR("PrimaryUsagePage"));
        int usage = prop_int(svc, CFSTR("PrimaryUsage"));

        fprintf(stderr, "  SPU device: page=0x%04X usage=%d\n", page, usage);

        if (page == 0xFF00 && usage == 3) {
            // Create HID device from registry entry
            IOHIDDeviceRef dev = IOHIDDeviceCreate(kCFAllocatorDefault, svc);
            if (dev) {
                IOReturn ret = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeSeizeDevice);
                if (ret != kIOReturnSuccess) {
                    // Try without seize
                    ret = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
                }
                if (ret == kIOReturnSuccess) {
                    found = dev;
                    IOObjectRelease(svc);
                    break;
                } else {
                    fprintf(stderr, "  ✗ Failed to open device: 0x%08x\n", ret);
                    CFRelease(dev);
                }
            }
        }
        IOObjectRelease(svc);
    }
    IOObjectRelease(iter);

    if (!found) {
        fprintf(stderr, "✗ Could not open SPU accelerometer (page=0xFF00 usage=3)\n");
        fprintf(stderr, "  Verify with: ioreg -l -w0 | grep -A5 AppleSPUHIDDevice\n");
        return 0;
    }

    g_accel.dev = found;
    g_accel.cal_target = (int)(CAL_SECONDS * ACCEL_RATE);

    // Register input report callback
    IOHIDDeviceRegisterInputReportCallback(
        found,
        g_accel.report_buf,
        REPORT_SIZE,
        accel_report_cb,
        NULL);

    IOHIDDeviceScheduleWithRunLoop(found, CFRunLoopGetCurrent(),
                                    kCFRunLoopDefaultMode);

    fprintf(stderr, "✓ SPU accelerometer opened (page=0xFF00 usage=3)\n");
    return 1;
}

// ── lid sensor (IOHIDManager, page=0x0020 usage=0x008a) ──────────

static struct {
    int            angle;
    int            orient;
    int            valid;
    IOHIDDeviceRef dev;
    CFArrayRef     elements;
} g_lid = {0};

static const char *orient_str(int s) {
    switch (s) {
        case 1: return "flat";
        case 2: return "portrait";
        case 3: return "landscape";
        default: return "unknown";
    }
}

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
    if (g_lid.dev) return;
    if (IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone) != kIOReturnSuccess) return;
    g_lid.dev      = dev;
    g_lid.elements = IOHIDDeviceCopyMatchingElements(dev, NULL, kIOHIDOptionsTypeNone);
    fprintf(stderr, "✓ Lid sensor opened\n");
}

static void setup_lid(void) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
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

// ── display ───────────────────────────────────────────────────────

static void display(CFRunLoopTimerRef timer, void *info) {
    poll_lid();

    FILE *f = stderr;
    fprintf(f, "\033[H\033[2J");
    fprintf(f, "+----------------------------------------------------+\n");
    fprintf(f, "|   MacBook Sensor Monitor             (ctrl+c quit) |\n");
    fprintf(f, "+----------------------------------------------------+\n");

    // Lid
    fprintf(f, "| LID ANGLE                                          |\n");
    if (g_lid.valid) {
        int bar = (int)(g_lid.angle / 180.0 * 30);
        if (bar > 30) bar = 30;
        char b[31]; memset(b, '#', bar); memset(b + bar, '-', 30 - bar);
        const char *desc =
            g_lid.angle < 5                          ? "closed"        :
            g_lid.angle < 20                         ? "nearly closed" :
            g_lid.angle >= 80 && g_lid.angle <= 110  ? "normal use"    :
            g_lid.angle >= 170 && g_lid.angle <= 190 ? "flat open"     :
            g_lid.angle > 190                        ? "flipped back"  : "";
        fprintf(f, "|  %3d deg  [%.30s]   |\n", g_lid.angle, b);
        fprintf(f, "|  orient: %-12s    %-18s  |\n",
                orient_str(g_lid.orient), desc);
    } else {
        fprintf(f, "|  waiting for lid sensor...                         |\n");
    }

    // Accelerometer
    fprintf(f, "+----------------------------------------------------+\n");
    fprintf(f, "| ACCELEROMETER (SPU 0xFF00:3)                       |\n");

    if (!g_accel.active) {
        fprintf(f, "|  waiting for data...                               |\n");
    } else if (!g_accel.cal_done) {
        int pct = g_accel.cal_target > 0
            ? g_accel.cal_count * 100 / g_accel.cal_target : 0;
        int bar = pct * 30 / 100; if (bar > 30) bar = 30;
        char b[31]; memset(b, '#', bar); memset(b + bar, '-', 30 - bar);
        fprintf(f, "|  calibrating [%.30s] %3d%%        |\n", b, pct);
    } else {
        fprintf(f, "|  x: %+7.3f  y: %+7.3f  z: %+7.3f  g          |\n",
                g_accel.x, g_accel.y, g_accel.z);

        double mag = g_accel.mag;
        int bar = (int)(mag / (IMPACT_THRESHOLD * 2) * 30);
        if (bar > 30) bar = 30; if (bar < 0) bar = 0;
        char b[31]; memset(b, '#', bar); memset(b + bar, '-', 30 - bar);
        fprintf(f, "|  force:[%.30s] %5.2fg  |\n", b, mag);
        fprintf(f, "|  threshold: %-5.2fg   impacts: %-16d  |\n",
                IMPACT_THRESHOLD, g_accel.impact_count);

        double t = now_sec();
        if (g_accel.last_impact_time > 0 &&
            t - g_accel.last_impact_time < 0.5) {
            fprintf(f, "|  *** IMPACT DETECTED ***                           |\n");
        } else {
            fprintf(f, "|  (quiet)                                           |\n");
        }
    }

    fprintf(f, "+----------------------------------------------------+\n");
    fflush(f);
}

// ── main ──────────────────────────────────────────────────────────

int main(int argc, const char *argv[]) {
    fprintf(stderr, "Starting MacBook Sensor Monitor...\n\n");

    if (getuid() != 0) {
        fprintf(stderr, "⚠ Not running as root. SPU access likely needs sudo.\n\n");
    }

    // Lid sensor (IOHIDManager)
    setup_lid();

    // SPU accelerometer (IOKit registry → IOHIDDeviceCreate)
    int accel_ok = setup_accel();
    if (!accel_ok) {
        fprintf(stderr, "\n  Accelerometer not found. Lid-only mode.\n");
        fprintf(stderr, "  Make sure you're on Apple Silicon (M2+) and running as root.\n\n");
    }

    // Display timer (10Hz)
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 0.5,
        0.1, 0, 0, display, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), t, kCFRunLoopDefaultMode);

    fprintf(stderr, "\nRunning... (ctrl+c to quit)\n");
    CFRunLoopRun();

    return 0;
}
