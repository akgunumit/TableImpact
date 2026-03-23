// precise_sensors.c — Raw, instant sensor monitor for Apple Silicon MacBooks
//
// Unlike sensors.c, this has:
//   - No calibration delay — values display from the first callback
//   - No impact threshold/cooldown — continuous live magnitude
//   - 30Hz display refresh for real-time feel
//   - Discovery of all SPU HID devices (not just accelerometer)
//   - Per-axis bars and running average delta
//
// Compile: clang -o precise_sensors precise_sensors.c -framework IOKit -framework CoreFoundation -lm
// Run:     sudo ./precise_sensors

#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>

// ── config ────────────────────────────────────────────────────────

#define REPORT_SIZE       128    // ALS sends 122-byte reports
#define EMA_ALPHA         0.05   // exponential moving average smoothing
#define AXIS_BAR_WIDTH    30     // characters for per-axis bars
#define DELTA_BAR_WIDTH   30     // characters for delta bar
#define LID_BAR_WIDTH     30     // characters for lid angle bar
#define BOX_WIDTH         62     // total box width (inner)
#define MAX_OTHER_SPU     8      // max other SPU devices to track
#define SENSOR_INTERVAL_HZ 100   // software throttle: max callbacks/sec
#define GYRO_CAL_SAMPLES  100    // samples to average for bias calibration (~1s at 100Hz)
#define GYRO_DEADZONE     0.3    // deg/s — ignore rates below this after bias removal

// ── sensor info lookup ───────────────────────────────────────────

typedef enum {
    STYPE_GYROSCOPE,
    STYPE_ALS,
    STYPE_UNKNOWN
} sensor_type_t;

typedef struct {
    int              page;
    int              usage;
    const char      *name;
    sensor_type_t    type;
} sensor_info_t;

static const sensor_info_t known_sensors[] = {
    { 0xFF00, 9, "Gyroscope",            STYPE_GYROSCOPE    },
    { 0xFF00, 4, "Ambient Light Sensor", STYPE_ALS          },
    { 0xFF00, 6, "Barometer",            STYPE_UNKNOWN      },
    // 0xFF00:3 = Accelerometer, handled in its own section
    // 0xFF00:5 = Unknown (616 Hz, possibly gravity vector)
    // 0x0020:0x8a = Lid sensor, handled in its own section
};

static const sensor_info_t *lookup_sensor(int page, int usage) {
    for (int i = 0; i < (int)(sizeof(known_sensors)/sizeof(known_sensors[0])); i++) {
        if (known_sensors[i].page == page && known_sensors[i].usage == usage)
            return &known_sensors[i];
    }
    return NULL;
}

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

// Read ALS lux from IORegistry (AppleSPUVD6286 → CurrentLux). No root needed.
// Returns -1 if not available.
static double poll_als_lux(void) {
    io_iterator_t iter = 0;
    CFMutableDictionaryRef match = IOServiceMatching("AppleSPUVD6286");
    if (!match) return -1;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS || !iter) return -1;

    double lux = -1;
    io_service_t svc;
    while ((svc = IOIteratorNext(iter)) != 0) {
        CFTypeRef ref = IORegistryEntryCreateCFProperty(
            svc, CFSTR("CurrentLux"), kCFAllocatorDefault, 0);
        if (ref) {
            if (CFGetTypeID(ref) == CFNumberGetTypeID())
                CFNumberGetValue((CFNumberRef)ref, kCFNumberDoubleType, &lux);
            CFRelease(ref);
        }
        IOObjectRelease(svc);
        if (lux >= 0) break;
    }
    IOObjectRelease(iter);
    return lux;
}

static double g_registry_lux = -1;  // updated each frame

static double now_sec(void) {
    return CFAbsoluteTimeGetCurrent();
}

// ── accelerometer state ───────────────────────────────────────────

static struct {
    IOHIDDeviceRef dev;
    uint8_t        report_buf[REPORT_SIZE];
    int            active;

    // raw values (no calibration)
    double x, y, z;
    double mag;          // sqrt(x^2 + y^2 + z^2)

    // running average for delta
    double ema_mag;      // exponential moving average of magnitude
    double delta;        // |mag - ema_mag|
    int    ema_init;     // whether EMA has been seeded

    // software throttle
    double last_report_time;

    // callback rate tracking
    int    cb_count;         // callbacks in current window
    int    cb_rate;          // last computed rate (Hz)
    double cb_window_start;  // start of current 1-second window
    long   total_samples;    // total callbacks received
} g_accel = {0};

static const double g_min_interval = 1.0 / SENSOR_INTERVAL_HZ;

static void accel_report_cb(void *ctx, IOReturn result, void *sender,
                             IOHIDReportType type, uint32_t reportID,
                             uint8_t *report, CFIndex length) {
    (void)ctx; (void)result; (void)sender; (void)type; (void)reportID;
    if (length < 18) return;

    // Software throttle — skip if too soon
    double t = now_sec();
    if (t - g_accel.last_report_time < g_min_interval) return;
    g_accel.last_report_time = t;

    int32_t rx = (int32_t)(report[6]  | (report[7]  << 8) |
                           (report[8]  << 16) | (report[9]  << 24));
    int32_t ry = (int32_t)(report[10] | (report[11] << 8) |
                           (report[12] << 16) | (report[13] << 24));
    int32_t rz = (int32_t)(report[14] | (report[15] << 8) |
                           (report[16] << 16) | (report[17] << 24));

    double ax = rx / 65536.0;
    double ay = ry / 65536.0;
    double az = rz / 65536.0;

    g_accel.x = ax;
    g_accel.y = ay;
    g_accel.z = az;
    g_accel.mag = sqrt(ax * ax + ay * ay + az * az);
    g_accel.active = 1;
    g_accel.total_samples++;

    // Exponential moving average for delta
    if (!g_accel.ema_init) {
        g_accel.ema_mag = g_accel.mag;
        g_accel.ema_init = 1;
    } else {
        g_accel.ema_mag = EMA_ALPHA * g_accel.mag +
                          (1.0 - EMA_ALPHA) * g_accel.ema_mag;
    }
    g_accel.delta = fabs(g_accel.mag - g_accel.ema_mag);

    // Callback rate tracking (1-second window)
    g_accel.cb_count++;
    if (t - g_accel.cb_window_start >= 1.0) {
        g_accel.cb_rate = g_accel.cb_count;
        g_accel.cb_count = 0;
        g_accel.cb_window_start = t;
    }
}

static int setup_accel(void) {
    CFMutableDictionaryRef match = IOServiceMatching("AppleSPUHIDDevice");
    if (!match)
        match = IOServiceMatching("AppleSPUHIDInterface");

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS || !iter) return 0;

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

    if (!found) return 0;

    g_accel.dev = found;
    g_accel.cb_window_start = now_sec();

    IOHIDDeviceRegisterInputReportCallback(
        found, g_accel.report_buf, REPORT_SIZE, accel_report_cb, NULL);
    IOHIDDeviceScheduleWithRunLoop(found, CFRunLoopGetCurrent(),
                                    kCFRunLoopDefaultMode);
    return 1;
}

// ── lid sensor ────────────────────────────────────────────────────

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
    (void)ctx; (void)result; (void)sender;
    if (g_lid.dev) return;
    if (IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone) != kIOReturnSuccess) return;
    g_lid.dev      = dev;
    g_lid.elements = IOHIDDeviceCopyMatchingElements(dev, NULL, kIOHIDOptionsTypeNone);
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

// ── other SPU devices ─────────────────────────────────────────────

static struct {
    int            page;
    int            usage;
    const char    *name;        // human-readable name (from lookup or "Unknown Sensor")
    sensor_type_t  type;
    IOHIDDeviceRef dev;
    uint8_t        report_buf[REPORT_SIZE];
    uint8_t        latest_report[REPORT_SIZE];
    CFIndex        report_len;
    int            active;

    // Parsed values (populated in callback based on type)
    double         val_x, val_y, val_z;   // 3-axis sensors (gyro, mag)
    double         val_single;            // single-value sensors (ALS)

    // Gyro bias calibration + integration
    double         bias_x, bias_y, bias_z;   // calibrated bias (avg of first N samples)
    double         bias_sum_x, bias_sum_y, bias_sum_z;
    int            bias_samples;             // samples collected for calibration
    int            bias_done;                // 1 = calibration complete
    double         angle_x, angle_y, angle_z;
    double         last_cb_time;             // for computing dt

    // Software throttle
    double         last_report_time;

    // Callback rate tracking
    int            cb_count;
    int            cb_rate;
    double         cb_window_start;
} g_other_spu[MAX_OTHER_SPU];
static int g_other_spu_count = 0;

static void other_spu_report_cb(void *ctx, IOReturn result, void *sender,
                                  IOHIDReportType type, uint32_t reportID,
                                  uint8_t *report, CFIndex length) {
    (void)result; (void)sender; (void)type; (void)reportID;
    int idx = (int)(intptr_t)ctx;
    if (idx < 0 || idx >= g_other_spu_count) return;

    // Software throttle — skip if too soon
    double t_now = now_sec();
    if (t_now - g_other_spu[idx].last_report_time < g_min_interval) return;
    g_other_spu[idx].last_report_time = t_now;

    CFIndex copy_len = length < REPORT_SIZE ? length : REPORT_SIZE;
    memcpy(g_other_spu[idx].latest_report, report, copy_len);
    g_other_spu[idx].report_len = copy_len;
    g_other_spu[idx].active = 1;

    // Parse values based on sensor type
    switch (g_other_spu[idx].type) {
    case STYPE_GYROSCOPE:
        if (length >= 18) {
            int32_t rx = (int32_t)(report[6]  | (report[7]  << 8) |
                                   (report[8]  << 16) | (report[9]  << 24));
            int32_t ry = (int32_t)(report[10] | (report[11] << 8) |
                                   (report[12] << 16) | (report[13] << 24));
            int32_t rz = (int32_t)(report[14] | (report[15] << 8) |
                                   (report[16] << 16) | (report[17] << 24));
            double raw_x = rx / 65536.0;
            double raw_y = ry / 65536.0;
            double raw_z = rz / 65536.0;

            // Phase 1: Bias calibration (collect first N samples while stationary)
            if (!g_other_spu[idx].bias_done) {
                g_other_spu[idx].bias_sum_x += raw_x;
                g_other_spu[idx].bias_sum_y += raw_y;
                g_other_spu[idx].bias_sum_z += raw_z;
                g_other_spu[idx].bias_samples++;
                if (g_other_spu[idx].bias_samples >= GYRO_CAL_SAMPLES) {
                    double n = (double)g_other_spu[idx].bias_samples;
                    g_other_spu[idx].bias_x = g_other_spu[idx].bias_sum_x / n;
                    g_other_spu[idx].bias_y = g_other_spu[idx].bias_sum_y / n;
                    g_other_spu[idx].bias_z = g_other_spu[idx].bias_sum_z / n;
                    g_other_spu[idx].bias_done = 1;
                }
                g_other_spu[idx].val_x = raw_x;
                g_other_spu[idx].val_y = raw_y;
                g_other_spu[idx].val_z = raw_z;
                g_other_spu[idx].last_cb_time = now_sec();
                break;
            }

            // Phase 2: Subtract bias
            double cx = raw_x - g_other_spu[idx].bias_x;
            double cy = raw_y - g_other_spu[idx].bias_y;
            double cz = raw_z - g_other_spu[idx].bias_z;

            // Dead zone — ignore small noise to prevent drift
            if (fabs(cx) < GYRO_DEADZONE) cx = 0;
            if (fabs(cy) < GYRO_DEADZONE) cy = 0;
            if (fabs(cz) < GYRO_DEADZONE) cz = 0;

            g_other_spu[idx].val_x = cx;
            g_other_spu[idx].val_y = cy;
            g_other_spu[idx].val_z = cz;

            // Integrate corrected angular velocity into angle
            double t = now_sec();
            if (g_other_spu[idx].last_cb_time > 0) {
                double dt = t - g_other_spu[idx].last_cb_time;
                if (dt > 0 && dt < 0.1) {
                    g_other_spu[idx].angle_x += cx * dt;
                    g_other_spu[idx].angle_y += cy * dt;
                    g_other_spu[idx].angle_z += cz * dt;
                }
            }
            g_other_spu[idx].last_cb_time = t;
        }
        break;
    case STYPE_ALS:
        if (length >= 44) {
            // 122-byte ALS report: float LE at offset 40 = lux
            float lux;
            memcpy(&lux, report + 40, sizeof(float));
            g_other_spu[idx].val_single = (double)lux;
        }
        break;
    case STYPE_UNKNOWN:
        if (length >= 18) {
            int32_t rx = (int32_t)(report[6]  | (report[7]  << 8) |
                                   (report[8]  << 16) | (report[9]  << 24));
            int32_t ry = (int32_t)(report[10] | (report[11] << 8) |
                                   (report[12] << 16) | (report[13] << 24));
            int32_t rz = (int32_t)(report[14] | (report[15] << 8) |
                                   (report[16] << 16) | (report[17] << 24));
            g_other_spu[idx].val_x = (double)rx;
            g_other_spu[idx].val_y = (double)ry;
            g_other_spu[idx].val_z = (double)rz;
        } else if (length >= 10) {
            int32_t rv = (int32_t)(report[6] | (report[7] << 8) |
                                   (report[8] << 16) | (report[9] << 24));
            g_other_spu[idx].val_x = (double)rv;
            g_other_spu[idx].val_y = 0;
            g_other_spu[idx].val_z = 0;
        }
        break;
    }

    // Callback rate tracking (1-second window)
    double t = now_sec();
    g_other_spu[idx].cb_count++;
    if (t - g_other_spu[idx].cb_window_start >= 1.0) {
        g_other_spu[idx].cb_rate = g_other_spu[idx].cb_count;
        g_other_spu[idx].cb_count = 0;
        g_other_spu[idx].cb_window_start = t;
    }
}

static void discover_other_spu(void) {
    CFMutableDictionaryRef match = IOServiceMatching("AppleSPUHIDDevice");
    if (!match) {
        match = IOServiceMatching("AppleSPUHIDInterface");
        if (!match) return;
    }

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS || !iter) return;

    io_service_t svc;
    while ((svc = IOIteratorNext(iter)) != 0) {
        int page  = prop_int(svc, CFSTR("PrimaryUsagePage"));
        int usage = prop_int(svc, CFSTR("PrimaryUsage"));

        // Skip the accelerometer and lid — we handle them separately
        if ((page == 0xFF00 && usage == 3) ||
            (page == 0x0020 && usage == 0x8a)) {
            IOObjectRelease(svc);
            continue;
        }

        if (g_other_spu_count >= MAX_OTHER_SPU) {
            IOObjectRelease(svc);
            continue;
        }

        IOHIDDeviceRef dev = IOHIDDeviceCreate(kCFAllocatorDefault, svc);
        if (dev) {
            IOReturn ret = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeSeizeDevice);
            if (ret != kIOReturnSuccess)
                ret = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
            if (ret == kIOReturnSuccess) {
                int idx = g_other_spu_count;
                g_other_spu[idx].page = page;
                g_other_spu[idx].usage = usage;
                g_other_spu[idx].dev = dev;
                g_other_spu[idx].cb_window_start = now_sec();

                const sensor_info_t *info = lookup_sensor(page, usage);
                g_other_spu[idx].name = info ? info->name : "Unknown Sensor";
                g_other_spu[idx].type = info ? info->type : STYPE_UNKNOWN;

                IOHIDDeviceRegisterInputReportCallback(
                    dev, g_other_spu[idx].report_buf, REPORT_SIZE,
                    other_spu_report_cb, (void *)(intptr_t)idx);
                IOHIDDeviceScheduleWithRunLoop(dev, CFRunLoopGetCurrent(),
                                                kCFRunLoopDefaultMode);
                g_other_spu_count++;
                fprintf(stderr, "  %s (page=0x%04X usage=%d) — opened\n",
                        g_other_spu[idx].name, page, usage);
            } else {
                fprintf(stderr, "  SPU device: page=0x%04X usage=%d — failed to open\n",
                        page, usage);
                CFRelease(dev);
            }
        }
        IOObjectRelease(svc);
    }
    IOObjectRelease(iter);
}

// ── display helpers ───────────────────────────────────────────────

// Build a bar centered at middle, filling left/right based on value.
// range: value range for full bar (e.g. 2.0 means -2g to +2g)
static void axis_bar(char *buf, int width, double value, double range) {
    memset(buf, '-', width);
    int center = width / 2;
    double norm = value / range;  // -1.0 to +1.0
    if (norm > 1.0) norm = 1.0;
    if (norm < -1.0) norm = -1.0;
    int fill = (int)(fabs(norm) * center);
    if (fill > center) fill = center;
    if (norm >= 0) {
        for (int i = center; i < center + fill; i++) buf[i] = '#';
    } else {
        for (int i = center - fill; i < center; i++) buf[i] = '#';
    }
    buf[center] = '|';  // center marker
}

// Simple left-fill bar
static void fill_bar(char *buf, int width, double value, double max_val) {
    memset(buf, '-', width);
    double frac = value / max_val;
    if (frac > 1.0) frac = 1.0;
    if (frac < 0.0) frac = 0.0;
    int fill = (int)(frac * width);
    for (int i = 0; i < fill; i++) buf[i] = '#';
}

// Print a box line padded to BOX_WIDTH
static void box_line(FILE *f, const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    int len = (int)strlen(line);
    fprintf(f, "| %-*s |\n", BOX_WIDTH - 2, line);
    (void)len;
}

static void box_sep(FILE *f) {
    fprintf(f, "+");
    for (int i = 0; i < BOX_WIDTH; i++) fprintf(f, "-");
    fprintf(f, "+\n");
}

static volatile sig_atomic_t g_quit = 0;

// ── display ───────────────────────────────────────────────────────

static void display(CFRunLoopTimerRef timer, void *info) {
    (void)timer; (void)info;
    if (g_quit) {
        fprintf(stderr, "\033[?25h\n");
        CFRunLoopStop(CFRunLoopGetCurrent());
        return;
    }
    poll_lid();
    g_registry_lux = poll_als_lux();

    FILE *f = stderr;
    fprintf(f, "\033[H\033[2J");

    // Header
    box_sep(f);
    box_line(f, "  Precise Sensor Monitor                (ctrl+c quit)");
    box_sep(f);

    // Lid
    box_line(f, "LID ANGLE");
    if (g_lid.valid) {
        char bar[LID_BAR_WIDTH + 1];
        fill_bar(bar, LID_BAR_WIDTH, g_lid.angle, 180.0);
        bar[LID_BAR_WIDTH] = '\0';
        const char *desc =
            g_lid.angle < 5                          ? "closed"        :
            g_lid.angle < 20                         ? "nearly closed" :
            g_lid.angle >= 80 && g_lid.angle <= 110  ? "normal use"    :
            g_lid.angle >= 170 && g_lid.angle <= 190 ? "flat open"     :
            g_lid.angle > 190                        ? "flipped back"  : "";
        char tmp[128];
        snprintf(tmp, sizeof(tmp), " %3d deg  [%.30s]  %s",
                 g_lid.angle, bar, desc);
        box_line(f, "%s", tmp);
        snprintf(tmp, sizeof(tmp), " orient: %s", orient_str(g_lid.orient));
        box_line(f, "%s", tmp);
    } else {
        box_line(f, " waiting for lid sensor...");
    }

    // Accelerometer
    box_sep(f);
    box_line(f, "ACCELEROMETER (RAW - no calibration)");

    if (!g_accel.active) {
        box_line(f, " waiting for data...");
    } else {
        char tmp[128];

        // Raw x/y/z
        snprintf(tmp, sizeof(tmp), " x: %+7.3f   y: %+7.3f   z: %+7.3f   g",
                 g_accel.x, g_accel.y, g_accel.z);
        box_line(f, "%s", tmp);

        // Magnitude
        snprintf(tmp, sizeof(tmp), " magnitude: %.3fg", g_accel.mag);
        box_line(f, "%s", tmp);

        // Delta from running average
        char dbar[DELTA_BAR_WIDTH + 1];
        fill_bar(dbar, DELTA_BAR_WIDTH, g_accel.delta, 0.5);
        dbar[DELTA_BAR_WIDTH] = '\0';
        snprintf(tmp, sizeof(tmp), " delta:     %.3fg  [%.30s]",
                 g_accel.delta, dbar);
        box_line(f, "%s", tmp);

        // Per-axis bars (centered, +-2g range)
        char xbar[AXIS_BAR_WIDTH + 1], ybar[AXIS_BAR_WIDTH + 1], zbar[AXIS_BAR_WIDTH + 1];
        axis_bar(xbar, AXIS_BAR_WIDTH, g_accel.x, 2.0);
        axis_bar(ybar, AXIS_BAR_WIDTH, g_accel.y, 2.0);
        axis_bar(zbar, AXIS_BAR_WIDTH, g_accel.z, 2.0);
        xbar[AXIS_BAR_WIDTH] = '\0';
        ybar[AXIS_BAR_WIDTH] = '\0';
        zbar[AXIS_BAR_WIDTH] = '\0';

        // Print bars in a compact layout (two bars per line)
        snprintf(tmp, sizeof(tmp), " x:[%.15s] y:[%.15s]", xbar, ybar);
        box_line(f, "%s", tmp);
        snprintf(tmp, sizeof(tmp), " z:[%.15s]", zbar);
        box_line(f, "%s", tmp);

        // Callback rate and sample count
        snprintf(tmp, sizeof(tmp), " callback rate: %d Hz    samples: %ld",
                 g_accel.cb_rate, g_accel.total_samples);
        box_line(f, "%s", tmp);
    }

    // Other sensors
    box_sep(f);
    box_line(f, "OTHER SENSORS");

    int known_count = 0;
    for (int i = 0; i < g_other_spu_count; i++)
        if (g_other_spu[i].type != STYPE_UNKNOWN) known_count++;

    if (known_count == 0) {
        box_line(f, " (no additional sensors found)");
    } else {
        for (int i = 0; i < g_other_spu_count; i++) {
            if (g_other_spu[i].type == STYPE_UNKNOWN) continue;
            char tmp[128];
            if (g_other_spu[i].active) {
                snprintf(tmp, sizeof(tmp), " %s (0x%04X:%d)%*s%d Hz",
                         g_other_spu[i].name,
                         g_other_spu[i].page, g_other_spu[i].usage,
                         (int)(40 - strlen(g_other_spu[i].name)), "",
                         g_other_spu[i].cb_rate);
                box_line(f, "%s", tmp);

                switch (g_other_spu[i].type) {
                case STYPE_GYROSCOPE: {
                    if (!g_other_spu[i].bias_done) {
                        snprintf(tmp, sizeof(tmp),
                                 "   calibrating... %d/%d  (keep still)",
                                 g_other_spu[i].bias_samples, GYRO_CAL_SAMPLES);
                        box_line(f, "%s", tmp);
                        break;
                    }
                    snprintf(tmp, sizeof(tmp),
                             "   rate: %+8.4f  %+8.4f  %+8.4f  deg/s",
                             g_other_spu[i].val_x, g_other_spu[i].val_y,
                             g_other_spu[i].val_z);
                    box_line(f, "%s", tmp);
                    snprintf(tmp, sizeof(tmp),
                             "   angle: %+7.1f  %+7.1f  %+7.1f  deg",
                             g_other_spu[i].angle_x, g_other_spu[i].angle_y,
                             g_other_spu[i].angle_z);
                    box_line(f, "%s", tmp);
                    // Per-axis bars based on accumulated angle (centered, +-90 deg)
                    char gxb[AXIS_BAR_WIDTH+1], gyb[AXIS_BAR_WIDTH+1], gzb[AXIS_BAR_WIDTH+1];
                    axis_bar(gxb, AXIS_BAR_WIDTH, g_other_spu[i].angle_x, 90.0);
                    axis_bar(gyb, AXIS_BAR_WIDTH, g_other_spu[i].angle_y, 90.0);
                    axis_bar(gzb, AXIS_BAR_WIDTH, g_other_spu[i].angle_z, 90.0);
                    gxb[AXIS_BAR_WIDTH] = '\0';
                    gyb[AXIS_BAR_WIDTH] = '\0';
                    gzb[AXIS_BAR_WIDTH] = '\0';
                    snprintf(tmp, sizeof(tmp), "   x:[%.15s] y:[%.15s]", gxb, gyb);
                    box_line(f, "%s", tmp);
                    snprintf(tmp, sizeof(tmp), "   z:[%.15s]", gzb);
                    box_line(f, "%s", tmp);
                    break;
                }
                case STYPE_ALS: {
                    if (g_registry_lux >= 0) {
                        snprintf(tmp, sizeof(tmp),
                                 "   brightness: %.1f lux  (registry: %.0f lux)",
                                 g_other_spu[i].val_single, g_registry_lux);
                    } else {
                        snprintf(tmp, sizeof(tmp),
                                 "   brightness: %.1f lux  (registry: n/a)",
                                 g_other_spu[i].val_single);
                    }
                    box_line(f, "%s", tmp);
                    // Lux bar (0-500 lux range)
                    char lbar[LID_BAR_WIDTH+1];
                    fill_bar(lbar, LID_BAR_WIDTH, g_other_spu[i].val_single, 500.0);
                    lbar[LID_BAR_WIDTH] = '\0';
                    snprintf(tmp, sizeof(tmp), "   [%.30s]", lbar);
                    box_line(f, "%s", tmp);
                    break;
                }
                case STYPE_UNKNOWN:
                    if (g_other_spu[i].report_len >= 18) {
                        snprintf(tmp, sizeof(tmp),
                                 "   v1: %.0f   v2: %.0f   v3: %.0f",
                                 g_other_spu[i].val_x, g_other_spu[i].val_y,
                                 g_other_spu[i].val_z);
                    } else if (g_other_spu[i].report_len >= 10) {
                        snprintf(tmp, sizeof(tmp),
                                 "   v1: %.0f",
                                 g_other_spu[i].val_x);
                    } else {
                        snprintf(tmp, sizeof(tmp),
                                 "   (report too short: %d bytes)",
                                 (int)g_other_spu[i].report_len);
                    }
                    box_line(f, "%s", tmp);
                    break;
                }
            } else {
                snprintf(tmp, sizeof(tmp), " %s (0x%04X:%d)  waiting for data...",
                         g_other_spu[i].name,
                         g_other_spu[i].page, g_other_spu[i].usage);
                box_line(f, "%s", tmp);
            }
        }
    }

    box_sep(f);
    fflush(f);
}

// ── signal handling ────────────────────────────────────────────────

static void sigint_handler(int sig) {
    (void)sig;
    g_quit = 1;
    CFRunLoopStop(CFRunLoopGetMain());
}

// ── main ──────────────────────────────────────────────────────────

int main(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    fprintf(stderr, "Starting Precise Sensor Monitor...\n\n");

    if (getuid() != 0) {
        fprintf(stderr, "⚠ Not running as root. SPU access likely needs sudo.\n\n");
    }

    // Lid sensor
    setup_lid();

    // Accelerometer
    int accel_ok = setup_accel();
    if (accel_ok) {
        fprintf(stderr, "✓ SPU accelerometer opened (page=0xFF00 usage=3)\n");
    } else {
        fprintf(stderr, "✗ Could not open SPU accelerometer (page=0xFF00 usage=3)\n");
        fprintf(stderr, "  Verify with: ioreg -l -w0 | grep -A5 AppleSPUHIDDevice\n");
    }

    // Discover other SPU devices
    fprintf(stderr, "\nScanning for other SPU devices...\n");
    discover_other_spu();
    if (g_other_spu_count == 0) {
        fprintf(stderr, "  (no additional SPU devices found)\n");
    }

    // Display timer at 120Hz
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 0.1,
        1.0 / 120.0,  // 120Hz
        0, 0, display, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), t, kCFRunLoopDefaultMode);

    fprintf(stderr, "\nRunning at 120Hz... (ctrl+c to quit)\n");
    CFRunLoopRun();

    return 0;
}
