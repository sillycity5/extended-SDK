#include "ymsdk.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace BNM;
using BNM::IL2CPP::Il2CppObject;

YEEPS_MOD("Signature Dump")

static std::string TypeName(const BNM::IL2CPP::Il2CppType *t) {
    Class c(t);
    if (!c.IsValid() || !c.GetClass() || !c.GetClass()->name) return "?";
    auto *k = c.GetClass();
    return (k->namespaze && k->namespaze[0]) ? std::string(k->namespaze) + "." + k->name : k->name;
}

static bool Readable(const char *n) {
    if (!n || !*n) return false;
    for (const char *p = n; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '<' || c == '>' || c == '`')) return false;
    }
    return true;
}

static void Line(FILE *f, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
}

static void DumpClass(FILE *f, Class c, const char *label, int cap) {
    if (!c.IsValid()) { Line(f, "## %s: CLASS MISSING", label); return; }
    auto *k = c.GetClass();
    Line(f, "## %s", label);
    Line(f, "   size=%d fields=%d methods=%d", k ? k->instance_size : 0,
         k ? k->field_count : 0, k ? k->method_count : 0);
    if (!k) return;
    for (int i = 0; i < k->field_count; i++) {
        auto *fi = &k->fields[i];
        if (!fi->name || !Readable(fi->name)) continue;
        Line(f, "   field %s : %s @%X", fi->name, TypeName(fi->type).c_str(), fi->offset);
    }
    int n = 0;
    for (auto &m : c.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name || !Readable(mi->name)) continue;
        std::string sig;
        for (uint32_t i = 0; i < mi->parameters_count; i++) {
            if (i) sig += ',';
            sig += TypeName(mi->parameters[i]);
        }
        Line(f, "   %s(%s)", mi->name, sig.c_str());
        if (++n >= cap) { Line(f, "   capped at %d", cap); break; }
    }
}

static void DumpAll(const char *const *names, int count) {
    const char *dir = ym::api() ? ym::api()->modsDir : nullptr;
    if (!dir) return;
    char path[600];
    snprintf(path, sizeof path, "%s/signatures.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    Line(f, "%s %s", ym::api()->gameVersion ? ym::api()->gameVersion : "?", ym::api()->buildId ? ym::api()->buildId : "?");
    for (int i = 0; i < count; i++) {
        Class c("", names[i], Image("Assembly-CSharp.dll"));
        DumpClass(f, c, names[i], 400);
    }
    fclose(f);
    ym::log("signatures: wrote %d classes", count);
}

static const char *kNames[] = {
    "Item", "ItemManager", "ItemData", "Player", "MasterPlayer", "PuppetPlayer",
    "NetworkMessenger", "MapManager", "MapObject", "MobManager", "MobData",
    "SuperpowerManager", "StuffingManager", "GameManager", "PhotonView"
};

static int g_frames = 0;

static void OnUpdate(float dt) {
    if (g_frames < 0) return;
    if (++g_frames < 180) return;
    g_frames = -1;
    DumpAll(kNames, (int)(sizeof(kNames) / sizeof(kNames[0])));
}

YM_EXPORT int YeepsMod_Init(const YeepsModApi *api) {
    if (!ym::init(api)) return -1;
    ym::onUpdate(OnUpdate);
    return 0;
}
