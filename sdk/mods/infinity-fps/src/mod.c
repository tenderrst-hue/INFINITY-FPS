/*
 * INFINITY FPS for Bearite / Super Bear Adventure.
 * Adjusts Unity targetFrameRate and can replace the FPS text shown in the HUD.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bearite_api.h"
#include "bearite_il2cpp.h"

static const BeariteApi* g_api;
static const char* g_id = "infinity-fps";
static void (*orig_set_target)(int value, const void* m);
static void (*orig_set_vsync)(int value, const void* m);
static void* (*orig_get_main)(const void* m);
static void (*orig_text_set)(void* self, void* str, const void* m);
static void (*orig_tmp_set)(void* self, void* str, const void* m);
static int g_busy, g_frozen, g_fake_logged;
static bool (*orig_fps_move)(void* self, const void* m);
static float g_saved_scale = 1.0f;
static double g_next;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static int real_on(void) { return g_api->get_bool(g_id, "real_on", 1); }
static long long get_num(const char* key, long long def, int* ok) {
    char buf[64];
    const char* s = g_api->get_string(g_id, key, "");
    snprintf(buf, sizeof buf, "%s", s ? s : "");
    char* e = NULL;
    long long v = strtoll(buf, &e, 10);
    int good = (e != buf);
    if (ok) *ok = good;
    return good ? v : def;
}
static int desired_target(int* freeze) {
    int ok;
    long long v = get_num("fps_value", 60, &ok);
    *freeze = 0;
    if (!ok) v = 60;
    if (v <= 0) { *freeze = 1; return 1; }
    if (v > 2000000000LL) v = 2000000000LL;
    return (int)v;
}
static void set_time_scale(float v) {
    void* m = bearite_find_method("UnityEngine", "Time", "set_timeScale", 1);
    if (!m) return;
    void* a[1] = { &v };
    bearite_invoke(m, NULL, a);
}
static float get_time_scale(void) {
    void* m = bearite_find_method("UnityEngine", "Time", "get_timeScale", 0);
    if (!m) return 1.0f;
    void* r = bearite_invoke(m, NULL, NULL);
    return r ? *(float*)((char*)r + 16) : 1.0f;
}
static void call_int(const char* cls, const char* name, int v) {
    void* m = bearite_find_method("UnityEngine", cls, name, 1);
    if (!m) return;
    void* a[1] = { &v };
    bearite_invoke(m, NULL, a);
}
static int g_last_target = -12345;
static void apply_real(void) {
    if (!real_on()) {
        if (g_frozen) { set_time_scale(g_saved_scale > 0 ? g_saved_scale : 1.0f); g_frozen = 0; }
        g_last_target = -12345;
        return;
    }
    int freeze;
    int target = desired_target(&freeze);
    if (freeze && !g_frozen) { g_saved_scale = get_time_scale(); if (g_saved_scale <= 0) g_saved_scale = 1.0f; set_time_scale(0.0f); g_frozen = 1; }
    if (!freeze && g_frozen) { set_time_scale(g_saved_scale); g_frozen = 0; }
    call_int("QualitySettings", "set_vSyncCount", 0);
    call_int("Application", "set_targetFrameRate", target);
    if (target != g_last_target) {
        char msg[96];
        snprintf(msg, sizeof msg, "Применён целевой FPS: %d%s", target, freeze ? " (пауза)" : "");
        g_api->log(g_id, msg);
        g_last_target = target;
    }
}
static void hook_set_target(int value, const void* m) {
    if (real_on()) { int fr; value = desired_target(&fr); }
    orig_set_target(value, m);
}
static void hook_set_vsync(int value, const void* m) { orig_set_vsync(real_on() ? 0 : value, m); }
static void* hook_get_main(const void* m) {
    void* cam = orig_get_main(m);
    if (cam && !g_busy) {
        double t = now_s();
        if (t >= g_next) { g_busy = 1; g_next = t + 0.5; apply_real(); g_busy = 0; }
    }
    return cam;
}
static void* fake_string(void* s) {
    if (!s || !g_api->get_bool(g_id, "fake_on", 0)) return s;
    int32_t len = *(int32_t*)((char*)s + 16);
    if (len < 3 || len > 48) return s;
    const uint16_t* c = (const uint16_t*)((char*)s + 20);
    int has = 0;
    for (int i = 0; i + 2 < len; i++)
        if ((c[i] | 32) == 'f' && (c[i + 1] | 32) == 'p' && (c[i + 2] | 32) == 's') { has = 1; break; }
    if (!has) return s;
    int a = -1, b = -1;
    for (int i = 0; i < len; i++) if (c[i] >= '0' && c[i] <= '9') { a = i; break; }
    if (a < 0) return s;
    for (b = a; b < len && ((c[b] >= '0' && c[b] <= '9') || c[b] == '.'); b++) {}
    if (a > 0 && c[a - 1] == '-') a--;
    int ok;
    long long fake = get_num("fake_value", 99999, &ok);
    if (!ok) return s;
    char out[200];
    int o = 0;
    for (int i = 0; i < a && o < 60; i++) out[o++] = c[i] < 128 ? (char)c[i] : '?';
    o += snprintf(out + o, sizeof out - o, "%lld", fake);
    for (int i = b; i < len && o < 190; i++) {
        uint16_t ch = c[i];
        if (ch < 128) out[o++] = (char)ch;
        else if (ch < 0x800) { out[o++] = (char)(0xC0 | (ch >> 6)); out[o++] = (char)(0x80 | (ch & 63)); }
        else { out[o++] = (char)(0xE0 | (ch >> 12)); out[o++] = (char)(0x80 | ((ch >> 6) & 63)); out[o++] = (char)(0x80 | (ch & 63)); }
    }
    out[o] = 0;
    void* ns = bearite_il2.string_new(out);
    if (ns && !g_fake_logged) { g_fake_logged = 1; g_api->log(g_id, "Подмена FPS-счётчика активна"); }
    return ns ? ns : s;
}
static void hook_text_set(void* self, void* str, const void* m) { orig_text_set(self, fake_string(str), m); }
static void hook_tmp_set(void* self, void* str, const void* m) { orig_tmp_set(self, fake_string(str), m); }
#define OFF_CORO_THIS 0x20
#define OFF_GM_FPSDISPLAY 0x178
static bool hook_fps_move(void* self, const void* m) {
    bool r = orig_fps_move(self, m);
    if (self && g_api->get_bool(g_id, "fake_on", 0)) {
        void* gm = *(void**)((char*)self + OFF_CORO_THIS);
        void* tmp = gm ? *(void**)((char*)gm + OFF_GM_FPSDISPLAY) : NULL;
        int ok;
        long long fake = get_num("fake_value", 99999, &ok);
        if (tmp && ok) {
            char out[48];
            snprintf(out, sizeof out, "%lld FPS", fake);
            void* str = bearite_il2.string_new(out);
            void* sm = bearite_find_method("TMPro", "TMP_Text", "set_text", 1);
            if (str && sm) {
                void* a[1] = { str };
                bearite_invoke(sm, tmp, a);
                if (!g_fake_logged) { g_fake_logged = 1; g_api->log(g_id, "Подмена FPS-счётчика GameManager активна"); }
            }
        }
    }
    return r;
}
static void* find_nested(void* outer, const char* part) {
    void* (*get_nested)(void*, void**) = (void* (*)(void*, void**))g_api->il2cpp_symbol("il2cpp_class_get_nested_types");
    const char* (*get_name)(void*) = (const char* (*)(void*))g_api->il2cpp_symbol("il2cpp_class_get_name");
    if (!get_nested || !get_name || !outer) return NULL;
    void* it = NULL; void* k;
    while ((k = get_nested(outer, &it)) != NULL) {
        const char* n = get_name(k);
        if (n && strstr(n, part)) return k;
    }
    return NULL;
}
static void hook_fps_coroutine(void) {
    void* gm = bearite_find_class("", "GameManager");
    void* co = find_nested(gm, "CalculateFPS");
    void* mi = co ? bearite_il2.class_get_method_from_name(co, "MoveNext", 0) : NULL;
    void* code = bearite_method_code(mi);
    if (!code) { g_api->log(g_id, "CalculateFPS не найдена; используется запасной метод"); return; }
    int rc = g_api->hook(code, (void*)hook_fps_move, (void**)&orig_fps_move);
    g_api->log(g_id, rc == 0 ? "Перехват GameManager.CalculateFPS готов" : "Не удалось перехватить GameManager.CalculateFPS");
    if (rc != 0) orig_fps_move = NULL;
}
__attribute__((visibility("default")))
void bearite_mod_init(const BeariteApi* api, const char* mod_id) {
    if (!api || api->version != BEARITE_API_VERSION) return;
    g_api = api;
    if (mod_id) g_id = mod_id;
    api->log(g_id, "INFINITY FPS: запуск");
    if (!bearite_il2cpp_init(api)) { api->log(g_id, "IL2CPP не найден"); return; }
    bearite_hook_method(api, g_id, "UnityEngine", "Application", "set_targetFrameRate", 1, (void*)hook_set_target, (void**)&orig_set_target);
    bearite_hook_method(api, g_id, "UnityEngine", "QualitySettings", "set_vSyncCount", 1, (void*)hook_set_vsync, (void**)&orig_set_vsync);
    bearite_hook_method(api, g_id, "UnityEngine", "Camera", "get_main", 0, (void*)hook_get_main, (void**)&orig_get_main);
    if (bearite_hook_method(api, g_id, "UnityEngine.UI", "Text", "set_text", 1, (void*)hook_text_set, (void**)&orig_text_set) != 0) orig_text_set = NULL;
    if (bearite_hook_method(api, g_id, "TMPro", "TMP_Text", "set_text", 1, (void*)hook_tmp_set, (void**)&orig_tmp_set) != 0) orig_tmp_set = NULL;
    hook_fps_coroutine();
    api->log(g_id, "INFINITY FPS: готов");
}
