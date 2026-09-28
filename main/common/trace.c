// =====================================================================
//  SynthMiner  --  the flight recorder (see trace.h)
// =====================================================================

#include "common/trace.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "world/chunk.h"
#include "world/vfs_compat.h"

#define PATH_MAX_    192
#define BUF_BYTES    1024

static FILE* s_f;
static char  s_buf[BUF_BYTES];
static int   s_n;          // bytes held in s_buf
static long  s_written;    // bytes sent to the file, for the rotation cap
static int   s_lines;
static double s_now;      // set once a frame; see trace_set_time

// The last few edits, so an arriving mesh can say how long its section
// had been waiting. A ring: an edit nobody meshes simply ages out, and
// four is more than the number of sections one swing can dirty.
#define PENDING 8
static struct {
    int32_t cx, cz;
    int     sect;
    double  t;
    bool    live;
} s_pending[PENDING];
static int s_pend_next;

static void flush(void) {
    if (s_f == NULL || s_n == 0) return;
    fwrite(s_buf, 1, (size_t)s_n, s_f);
    fflush(s_f);
    s_written += s_n;
    s_n = 0;
}

static void emit(char const* fmt, va_list ap) {
    if (s_f == NULL) return;
    char line[256];
    int const n = vsnprintf(line, sizeof line, fmt, ap);
    if (n <= 0) return;
    size_t const len = (size_t)n < sizeof line - 1 ? (size_t)n : sizeof line - 1;
    if (s_n + (int)len + 1 > BUF_BYTES) flush();
    if (s_n + (int)len + 1 > BUF_BYTES) return;   // a line longer than the buffer: drop it
    memcpy(s_buf + s_n, line, len);
    s_n += (int)len;
    s_buf[s_n++] = '\n';
    s_lines++;
}

static void say(char const* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(fmt, ap);
    va_end(ap);
}

void trace_note(char const* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(fmt, ap);
    va_end(ap);
}

static void path_of(char* out, size_t n, char const* dir, char const* name) {
    snprintf(out, n, "%s/%s", dir, name);
}

bool trace_open(char const* dir, char const* build, char const* engine, uint32_t seed, char const* world) {
    trace_close();

    char cur[PATH_MAX_], prev[PATH_MAX_];
    path_of(cur, sizeof cur, dir, "trace.txt");
    path_of(prev, sizeof prev, dir, "trace.prev.txt");

    // Rotate BEFORE opening, on the size the last session left behind.
    // One previous file is kept: enough to survive "I closed the game
    // and only then realised what I had seen".
    FILE* old = fopen(cur, "rb");
    if (old != NULL) {
        fseek(old, 0, SEEK_END);
        long const sz = ftell(old);
        fclose(old);
        if (sz > (long)TRACE_MAX_BYTES) {
            // Through vfs_compat, not stdio: graceloader exports neither
            // `remove` nor `rename`, and an app that calls them links
            // and then fails to LOAD (vfs_compat.h, F-06). symcheck
            // catches it, which is how this was found.
            sm_remove(prev);
            sm_rename(cur, prev);
        }
    }

    s_f = fopen(cur, "ab");
    if (s_f == NULL) return false;
    s_n = 0;
    s_written = 0;
    s_lines = 0;
    s_now = 0.0;
    memset(s_pending, 0, sizeof s_pending);
    s_pend_next = 0;

    say("H build=%s engine=%s seed=%u world=\"%s\"", build ? build : "?", engine ? engine : "?",
        (unsigned)seed, world ? world : "?");
    flush();
    return true;
}

void trace_close(void) {
    if (s_f == NULL) return;
    say("H closed lines=%d", s_lines);
    flush();
    fclose(s_f);
    s_f = NULL;
}

void trace_set_time(double t) {
    s_now = t;
}

void trace_tick(trace_tick_t const* s) {
    if (s_f == NULL || s == NULL) return;
    say("T t=%.1f fps=%.1f pos=%.1f,%.1f,%.1f yaw=%d", s->t, (double)s->fps, s->px, s->py, s->pz, s->yaw);
    // The drops go on their own line and only when there are any, so a
    // grep for "drop=" finds the frames that lost geometry and nothing
    // else. Everything that is always there stays on the line above.
    say("  flat=%d/%d tex=%d/%d drawn=%d/%d res=%d miss=%d queue=%d/%d mesh=%uKiB psram=%uKiB int=%uKiB",
        s->flat, s->flat_cap, s->tex, s->tex_cap, s->drawn, s->sections, s->resident, s->missing,
        s->queue, s->queue_cap, s->mesh_kib, s->psram_kib, s->internal_kib);
    if (s->drop_flat > 0 || s->drop_tex > 0) say("  drop=%d,%d", s->drop_flat, s->drop_tex);
    if (s->load_n > 0 || s->save_n > 0) {
        say("  io load=%d@%dus,max%dus save=%d@%dus,max%dus", s->load_n, s->load_avg_us, s->load_max_us,
            s->save_n, s->save_avg_us, s->save_max_us);
    }

    // Once a second is also the flush point: a crash then costs at most
    // the second it happened in, which is the second worth having.
    flush();
}

void trace_edit(char kind, int32_t x, int32_t y, int32_t z, char const* block, int extra) {
    if (s_f == NULL) return;
    int32_t const cx = chunk_of(x), cz = chunk_of(z);
    int const     s  = ch_sect_of((int)y);
    double const t = s_now;
    say("%c t=%.1f x=%d y=%d z=%d blk=%s ch=%d,%d s=%d n=%d", kind, t, (int)x, (int)y, (int)z,
        block ? block : "?", (int)cx, (int)cz, s, extra);

    s_pending[s_pend_next].cx   = cx;
    s_pending[s_pend_next].cz   = cz;
    s_pending[s_pend_next].sect = s;
    s_pending[s_pend_next].t    = t;
    s_pending[s_pend_next].live = true;
    s_pend_next            = (s_pend_next + 1) % PENDING;
}

void trace_mesh(int32_t cx, int32_t cz, int lod, int sect) {
    if (s_f == NULL) return;
    // Only meshes that answer an edit are worth a line -- the streamer
    // builds hundreds a minute and they are already counted in the T
    // line's rates. What is NOT counted anywhere is this lag.
    double const t = s_now;
    for (int i = 0; i < PENDING; i++) {
        if (!s_pending[i].live) continue;
        if (s_pending[i].cx != cx || s_pending[i].cz != cz || s_pending[i].sect != sect) continue;
        say("M t=%.1f ch=%d,%d s=%d lod=%d lag=%dms", t, (int)cx, (int)cz, sect, lod,
            (int)((t - s_pending[i].t) * 1000.0 + 0.5));
        if (lod == 0) s_pending[i].live = false;   // the detailed one is the one the player sees
        return;
    }
}

int trace_lines(void) {
    return s_lines;
}
