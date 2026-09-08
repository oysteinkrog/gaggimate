#include "DrawProfile.h"

#include <esp_timer.h>

namespace drawprof {

volatile int g_req = 0;

#ifdef GM_DRAW_PROFILE

namespace {

struct Entry {
    lv_obj_t *obj;
    const lv_obj_class_t *cls;
    lv_area_t coords;
    uint32_t mainUs;
    uint32_t postUs;
    uint32_t partUs; // DRAW_PART_BEGIN to DRAW_PART_END, the draw calls themselves
    uint32_t n;
};

constexpr int kMaxEntries = 128;
Entry g_entries[kMaxEntries];
int g_count = 0;
lv_obj_t *g_root = nullptr;
int64_t g_t0 = 0;
int64_t g_tPart = 0;
int64_t g_tLastEnd = 0; // end of the previous object's main or post draw
uint32_t g_gapUs = 0;   // time between one object's draw end and the next begin
uint32_t g_gapN = 0;

Entry *find(lv_obj_t *obj) {
    for (int i = 0; i < g_count; i++) {
        if (g_entries[i].obj == obj) {
            return &g_entries[i];
        }
    }
    return nullptr;
}

void cb(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DRAW_MAIN_BEGIN || code == LV_EVENT_DRAW_POST_BEGIN) {
        g_t0 = esp_timer_get_time();
        // A gap only counts within one refresh: anything over 50 ms is the
        // wait for the next pass, not LVGL walking to the next object.
        if (g_tLastEnd != 0 && g_t0 - g_tLastEnd < 50000) {
            g_gapUs += static_cast<uint32_t>(g_t0 - g_tLastEnd);
            g_gapN++;
        }
        return;
    }
    if (code == LV_EVENT_DRAW_PART_BEGIN) {
        g_tPart = esp_timer_get_time();
        return;
    }
    if (code == LV_EVENT_DRAW_PART_END) {
        Entry *en = find(lv_event_get_target(e));
        if (en != nullptr) {
            en->partUs += static_cast<uint32_t>(esp_timer_get_time() - g_tPart);
        }
        return;
    }
    if (code != LV_EVENT_DRAW_MAIN_END && code != LV_EVENT_DRAW_POST_END) {
        return;
    }
    Entry *en = find(lv_event_get_target(e));
    if (en == nullptr) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    g_tLastEnd = now;
    const uint32_t dt = static_cast<uint32_t>(now - g_t0);
    if (code == LV_EVENT_DRAW_MAIN_END) {
        en->mainUs += dt;
        en->n++;
    } else {
        en->postUs += dt;
    }
}

void attachRecurse(lv_obj_t *obj) {
    if (g_count < kMaxEntries) {
        Entry &en = g_entries[g_count++];
        en.obj = obj;
        en.cls = lv_obj_get_class(obj);
        lv_obj_get_coords(obj, &en.coords);
        en.mainUs = en.postUs = en.partUs = en.n = 0;
        // The flow engine keeps screens alive, so a revisit would stack a
        // second callback and double every count.
        lv_obj_remove_event_cb(obj, cb);
        lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, nullptr);
    }
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        attachRecurse(lv_obj_get_child(obj, i));
    }
}

const char *className(const lv_obj_class_t *cls) {
    if (cls == &lv_obj_class) {
        return "obj";
    }
    if (cls == &lv_img_class) {
        return "img";
    }
    if (cls == &lv_label_class) {
        return "label";
    }
    if (cls == &lv_btn_class) {
        return "btn";
    }
    if (cls == &lv_imgbtn_class) {
        return "imgbtn";
    }
    if (cls == &lv_meter_class) {
        return "meter";
    }
    if (cls == &lv_bar_class) {
        return "bar";
    }
    if (cls == &lv_arc_class) {
        return "arc";
    }
    if (cls == &lv_line_class) {
        return "line";
    }
    return "other";
}

} // namespace

void attach(lv_obj_t *root) {
    // The old root's callbacks die with its objects; if it is still alive
    // (the flow engine keeps some screens), its callbacks keep firing into
    // find() and miss, which is harmless.
    g_count = 0;
    g_root = root;
    g_gapUs = 0;
    g_gapN = 0;
    g_tLastEnd = 0;
    attachRecurse(root);
    // One screen per arm. Left armed, every screen the runner visited got a
    // callback on every object, and the touchmap reads an object with an
    // event callback as a tap target: 22 audit violations on status_icons
    // (2026-09-08).
    g_req = 0;
}

void report(JsonDocument &doc, const char *key, int maxEntries) {
    JsonArray arr = doc[key].to<JsonArray>();
    if (g_root == nullptr) {
        return;
    }
    uint32_t tm = 0, tp = 0, tpart = 0;
    for (int i = 0; i < g_count; i++) {
        tm += g_entries[i].mainUs;
        tp += g_entries[i].postUs;
        tpart += g_entries[i].partUs;
    }
    JsonObject tot = doc[String(key) + "_total"].to<JsonObject>();
    tot["objects"] = g_count;
    tot["main_us"] = tm;
    tot["post_us"] = tp;
    tot["part_us"] = tpart;
    tot["gap_us"] = g_gapUs;
    tot["gap_n"] = g_gapN;
    // Selection sort of at most maxEntries out of g_count: the table is small.
    bool used[kMaxEntries] = {false};
    for (int k = 0; k < maxEntries && k < g_count; k++) {
        int best = -1;
        for (int i = 0; i < g_count; i++) {
            if (used[i]) {
                continue;
            }
            const uint32_t t = g_entries[i].mainUs + g_entries[i].postUs;
            if (best < 0 || t > g_entries[best].mainUs + g_entries[best].postUs) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        used[best] = true;
        const Entry &en = g_entries[best];
        if (en.mainUs + en.postUs == 0) {
            break;
        }
        JsonObject o = arr.add<JsonObject>();
        o["cls"] = className(en.cls);
        o["x"] = en.coords.x1;
        o["y"] = en.coords.y1;
        o["w"] = lv_area_get_width(&en.coords);
        o["h"] = lv_area_get_height(&en.coords);
        o["main_us"] = en.mainUs;
        o["post_us"] = en.postUs;
        o["part_us"] = en.partUs;
        o["n"] = en.n;
        if (en.cls == &lv_img_class) {
            o["zoom"] = lv_img_get_zoom(en.obj);
        }
        if (en.cls == &lv_label_class) {
            o["text"] = lv_label_get_text(en.obj);
        }
    }
}

#endif // GM_DRAW_PROFILE

} // namespace drawprof
