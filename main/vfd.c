// =====================================================================
//  SynthMiner  --  optional external VFD status display (see vfd.h)
// ---------------------------------------------------------------------
//  Lifted from ../tanmatsu-synthracer-grace/main/vfd.c, with the three
//  things this game needs instead of that one's STAGE / PAUSED / marquee:
//  the item in hand, the game's name, and a name longer than the tube is
//  wide. The probe, the bus sharing and the shutdown are unchanged.
//
//  NE-HCS12SS59T-R1 register map (8-bit registers):
//    0       system control (bit 0 = enable, bit 1 = test, bit 2 = LED)
//    1       display offset into the text buffer
//    2       scroll length (maximum offset while auto-scrolling)
//    3       scroll mode (bits 3-0: 0 off / 1 left / 2 right; bit 4 loop)
//    4-5     scroll speed, ms per step (little-endian 16 bit)
//    6       filament current (default 110)
//    10-255  ASCII text buffer
// =====================================================================

#include "vfd.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/catt.h"
#include "bsp/i2c.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i18n/fold.h"
#include "i18n/i18n.h"
#include "i18n/strings_gen.h"  // SM_STR_COUNT: the range a published label may be

static char const TAG[] = "vfd";

#define VFD_ADDRESS     0x13    // 7-bit address (default 0x10 + jumpers 1 and 2)
#define VFD_SPEED_HZ    100000
#define VFD_CHARS       12      // visible characters
#define VFD_TIMEOUT_MS  50      // per-transfer timeout
#define VFD_PROBE_MS    20      // per-bus probe timeout

#define VFD_REG_CONTROL      0
#define VFD_REG_OFFSET       1
#define VFD_REG_SCROLL_LEN   2
#define VFD_REG_SCROLL_MODE  3
#define VFD_REG_SCROLL_SPEED 4
#define VFD_REG_BRIGHTNESS   6
#define VFD_REG_TEXT         10

#define VFD_CTRL_ENABLE      (1 << 0)
#define VFD_SCROLL_LEFT      1
#define VFD_SCROLL_LOOP      (1 << 4)

// Filament current. DON'T GO MUCH HIGHER: it drastically shortens the
// tube's life (same value as the vfdclock app and the device default).
#define VFD_BRIGHTNESS       110

// What the tube shows when the player is not standing in a world. Not a
// translated string on purpose: the game's name is not translated (i18n.h),
// and this is the one text here that is the same in every language.
#define VFD_TITLE_TEXT     "SynthMiner"
#define VFD_SCROLL_STEP_MS 200
#define VFD_POLL_MS        50  // task wake-up interval

#define VFD_TASK_STACK_BYTES (6 * 1024)  // ESP-IDF stack sizes are in bytes
// Core 1's ladder (chunk_worker.c): audio_mixer ~23, usbnet 10, se_stream 5,
// cmworker 4. A status display has the loosest deadline on this core -- a
// frame late on the tube is invisible -- so it goes under the chunk worker.
#define VFD_TASK_PRIORITY (tskIDLE_PRIORITY + 3)
#define VFD_TASK_CORE     1

// The hand-off is ONE aligned word, so the frame loop needs no lock: the
// mode in the top byte, the label string id (or VFD_NO_TEXT) below it.
// SM_STR_COUNT is a few hundred, so 24 bits is more than enough for a label.
#define VFD_NO_TEXT 0xFFFFFFu
#define VFD_WANT(show, label) (((uint32_t)(show) << 24) | ((uint32_t)(label) & 0xFFFFFFu))

// Longest text the tube is ever asked for. FOLD_MAX is what fold_text
// writes at most, and a scrolled name is framed by a blank screen width on
// both sides.
#define VFD_TEXT_MAX 64

typedef struct {
    char const*             name;
    i2c_master_bus_handle_t handle;
    bool                    shared;  // internal bus: shared with the coprocessor, needs claim/release
} vfd_bus_t;

static vfd_bus_t s_buses[2] = {
    {.name = "CATT I2C", .shared = false},
    {.name = "Internal I2C", .shared = true},
};

static vfd_bus_t*              s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

static volatile uint32_t s_want         = VFD_WANT(VFD_SHOW_TITLE, VFD_NO_ITEM);
static volatile bool     s_exit_request = false;
static volatile bool     s_task_done    = true;  // true until the task runs, and once it has finished

static void bus_lock(vfd_bus_t const* bus) {
    if (bus->shared) bsp_i2c_primary_bus_claim();
}

static void bus_unlock(vfd_bus_t const* bus) {
    if (bus->shared) bsp_i2c_primary_bus_release();
}

static esp_err_t vfd_write(uint8_t const* data, size_t len) {
    bus_lock(s_bus);
    esp_err_t const res = i2c_master_transmit(s_dev, data, len, VFD_TIMEOUT_MS);
    bus_unlock(s_bus);
    return res;
}

static esp_err_t vfd_write_reg(uint8_t reg, uint8_t value) {
    uint8_t const data[2] = {reg, value};
    return vfd_write(data, sizeof(data));
}

// Write `text` at the start of the text buffer, space-padded to at least
// `min_len` characters. Padding is what erases the previous, longer word.
static esp_err_t vfd_write_text(char const* text, size_t min_len) {
    uint8_t buf[1 + VFD_TEXT_MAX + 2 * VFD_CHARS];
    size_t  len = strlen(text);
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    if (min_len > sizeof(buf) - 1) min_len = sizeof(buf) - 1;
    size_t const total = (len > min_len) ? len : min_len;

    buf[0] = VFD_REG_TEXT;
    memcpy(&buf[1], text, len);
    memset(&buf[1 + len], ' ', total - len);
    return vfd_write(buf, 1 + total);
}

// Static text, centred on the 12 visible characters. An empty string is a
// blank line, which is what an empty hand is.
static esp_err_t vfd_show_static(char const* text) {
    char   line[VFD_CHARS + 1];
    size_t len = strlen(text);
    if (len > VFD_CHARS) len = VFD_CHARS;
    size_t const lead = (VFD_CHARS - len) / 2;
    memset(line, ' ', lead);
    memcpy(&line[lead], text, len);
    line[lead + len] = '\0';

    esp_err_t res = vfd_write_reg(VFD_REG_SCROLL_MODE, 0);  // stop scrolling first so the offset holds
    if (res == ESP_OK) res = vfd_write_reg(VFD_REG_OFFSET, 0);
    if (res == ESP_OK) res = vfd_write_text(line, VFD_CHARS);
    return res;
}

// A name too long for the tube, scrolled in from the right by the display
// itself. Framed by a blank screen width on both sides; the loop wraps
// after the last character leaves on the left, landing on the leading
// blank screen, so the marquee repeats seamlessly.
static esp_err_t vfd_show_scroller(char const* text, size_t len) {
    char buffer[VFD_TEXT_MAX + 2 * VFD_CHARS];
    snprintf(buffer, sizeof(buffer), "%*s%s", VFD_CHARS, "", text);
    size_t  const total     = VFD_CHARS + len;
    uint8_t const scroll_len = (uint8_t)(total - 1);  // last offset shows the final character alone
    uint8_t const speed[3]   = {VFD_REG_SCROLL_SPEED, VFD_SCROLL_STEP_MS & 0xFF, (VFD_SCROLL_STEP_MS >> 8) & 0xFF};

    esp_err_t res = vfd_write_reg(VFD_REG_SCROLL_MODE, 0);
    if (res == ESP_OK) res = vfd_write_reg(VFD_REG_OFFSET, 0);
    if (res == ESP_OK) res = vfd_write_text(buffer, total + VFD_CHARS);  // trailing blank screen width
    if (res == ESP_OK) res = vfd_write_reg(VFD_REG_SCROLL_LEN, scroll_len);
    if (res == ESP_OK) res = vfd_write(speed, sizeof(speed));
    if (res == ESP_OK) res = vfd_write_reg(VFD_REG_SCROLL_MODE, VFD_SCROLL_LEFT | VFD_SCROLL_LOOP);
    return res;
}

static esp_err_t vfd_show_text(char const* text) {
    size_t const len = strlen(text);
    if (len <= VFD_CHARS) return vfd_show_static(text);
    return vfd_show_scroller(text, len);
}

// Folded PER WORD, because fold_text drops spaces and a display without
// them is unreadable: the search box wants "ironpick" to match
// "ironpickaxe", and the tube wants "kobasica od pasulja" rather than
// "kobasicoodpasulja". Same table, same fold, one word at a time.
static void vfd_fold_words(char const* src, char* out, size_t cap) {
    size_t      n   = 0;
    char const* p   = src;
    out[0] = '\0';

    while (*p != '\0' && n + 1 < cap) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        char const* end = p;
        size_t      bytes = 0;
        while (*end != '\0' && *end != ' ' && bytes < FOLD_MAX - 1) {
            end++;
            bytes++;
        }

        char  word[FOLD_MAX];
        char  folded[FOLD_MAX];
        memcpy(word, p, bytes);
        word[bytes] = '\0';
        fold_text(word, folded, sizeof(folded));

        // A word that folds to nothing (a lone apostrophe) adds no space
        // either, so the line cannot grow leading or double spaces.
        if (folded[0] == '\0') {
            p = end;
            continue;
        }
        if (n > 0 && n + 1 < cap) out[n++] = ' ';
        size_t const len = strlen(folded);
        size_t const take = (n + len < cap) ? len : cap - n - 1;
        memcpy(&out[n], folded, take);
        n += take;
        out[n] = '\0';
        p = end;
    }
}

// What the tube should read, in characters it can actually burn.
//
// The label is published as a string id rather than as text, so the frame
// loop's side of the hand-off stays one word. Resolving it here means the
// task reads the player's language when it writes, which is also how a
// language changed in Settings reaches the display without the app having
// to publish anything again.
static void vfd_resolve(uint32_t want, char* out, size_t cap) {
    vfd_show_t const show  = (vfd_show_t)(want >> 24);
    int const        label = (int)(want & 0xFFFFFFu);

    if (show == VFD_SHOW_TITLE) {
        snprintf(out, cap, "%s", VFD_TITLE_TEXT);
        return;
    }
    if (label == (int)VFD_NO_TEXT || label < 0 || label >= SM_STR_COUNT) {
        out[0] = '\0';  // an empty hand
        return;
    }

    char const* src = T((sm_str_t)label);
    if (src == NULL) src = "";

    // The tube has 95 printable ASCII characters. A label outside them goes
    // through the fold the crafting book's search box already uses, so a
    // Cyrillic or accented name reads as its transliteration rather than as
    // garbage -- the same reason a player of that language types it that way
    // (i18n/fold.h).
    bool ascii = true;
    for (char const* p = src; *p != '\0'; p++) {
        unsigned char const c = (unsigned char)*p;
        if (c < ' ' || c > 0x7E) {
            ascii = false;
            break;
        }
    }
    if (ascii) snprintf(out, cap, "%s", src);
    else vfd_fold_words(src, out, cap);
}

// Look for the VFD on the CATT bus, then the internal bus, and attach to
// the first bus it answers on.
static bool vfd_probe(void) {
    // CATT bus: bsp_device_initialize() leaves it down if an add-on held the lines, so retry once
    if (bsp_catt_i2c_bus_get_handle(&s_buses[0].handle) != ESP_OK) {
        s_buses[0].handle = NULL;
        if (bsp_catt_set_i2c_enabled(true) != ESP_OK ||
            bsp_catt_i2c_bus_get_handle(&s_buses[0].handle) != ESP_OK) {
            s_buses[0].handle = NULL;
        }
    }
    if (bsp_i2c_primary_bus_get_handle(&s_buses[1].handle) != ESP_OK) {
        s_buses[1].handle = NULL;
    }

    for (size_t i = 0; i < sizeof(s_buses) / sizeof(s_buses[0]); i++) {
        vfd_bus_t* const bus = &s_buses[i];
        if (bus->handle == NULL) continue;
        bus_lock(bus);
        esp_err_t const res = i2c_master_probe(bus->handle, VFD_ADDRESS, VFD_PROBE_MS);
        bus_unlock(bus);
        if (res == ESP_OK) {
            s_bus = bus;
            break;
        }
    }
    if (s_bus == NULL) {
        ESP_LOGI(TAG, "no VFD at 0x%02X", VFD_ADDRESS);
        return false;
    }

    i2c_device_config_t const dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = VFD_ADDRESS,
        .scl_speed_hz    = VFD_SPEED_HZ,
    };
    esp_err_t const res = i2c_master_bus_add_device(s_bus->handle, &dev_cfg, &s_dev);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "failed to add VFD device: %s", esp_err_to_name(res));
        s_dev = NULL;
        return false;
    }
    ESP_LOGI(TAG, "VFD at 0x%02X on %s", VFD_ADDRESS, s_bus->name);
    return true;
}

static void vfd_task(void* arg) {
    (void)arg;

    if (vfd_probe()) {
        // A previous app may have left the display off, or scrolling.
        vfd_write_reg(VFD_REG_SCROLL_MODE, 0);
        vfd_write_reg(VFD_REG_BRIGHTNESS, VFD_BRIGHTNESS);
        vfd_write_reg(VFD_REG_CONTROL, VFD_CTRL_ENABLE);

        char shown[VFD_TEXT_MAX];
        bool have_shown = false;  // forces the first write
        bool write_ok   = true;

        while (!s_exit_request) {
            char text[VFD_TEXT_MAX];
            vfd_resolve(s_want, text, sizeof(text));

            // THE TEXT DECIDES, not the published word: a language changed in
            // Settings changes what the label says without the app publishing
            // anything, and the tube should follow it.
            esp_err_t res = ESP_OK;
            bool      wrote = false;
            if (!have_shown || strcmp(text, shown) != 0) {
                res   = vfd_show_text(text);
                wrote = true;
                if (res == ESP_OK) {
                    snprintf(shown, sizeof(shown), "%s", text);
                    have_shown = true;
                }
                // A write that did not make it is retried on the next poll,
                // because `shown` never took it.
            }

            if (wrote && (res == ESP_OK) != write_ok) {
                write_ok = (res == ESP_OK);
                if (write_ok) ESP_LOGI(TAG, "VFD writes recovered");
                else          ESP_LOGW(TAG, "VFD write failed: %s", esp_err_to_name(res));
            }
            vTaskDelay(pdMS_TO_TICKS(VFD_POLL_MS));
        }

        // Leave the tube dark: blank the text, stop scrolling, switch off.
        vfd_write_reg(VFD_REG_SCROLL_MODE, 0);
        vfd_write_reg(VFD_REG_OFFSET, 0);
        vfd_write_text("", VFD_CHARS);
        vfd_write_reg(VFD_REG_CONTROL, 0);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }

    s_task_done = true;
    vTaskDelete(NULL);
}

void vfd_init(void) {
    s_exit_request = false;
    s_task_done    = false;
    BaseType_t const ok = xTaskCreatePinnedToCore(vfd_task, "vfd", VFD_TASK_STACK_BYTES, NULL,
                                                  VFD_TASK_PRIORITY, NULL, VFD_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create VFD task");
        s_task_done = true;
    }
}

void vfd_show(vfd_show_t show, int label) {
    if (show != VFD_SHOW_ITEM) label = VFD_NO_ITEM;
    s_want = VFD_WANT(show, label);
}

void vfd_shutdown(void) {
    s_exit_request = true;
    // The task finishes its current write (at most a few bus timeouts),
    // then blanks and powers the display down. Bound the wait so a hung
    // bus can never keep the app from exiting.
    for (int i = 0; i < 100 && !s_task_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
