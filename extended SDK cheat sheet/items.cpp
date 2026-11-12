static constexpr int kMaxKeys = 512, kKeyLen = 48;

struct ItemKeys { char keys[kMaxKeys][kKeyLen]; int n = 0; };
static ItemKeys g_keys;
static int g_sel = -1;

struct Seq { uint8_t *base = nullptr; size_t n = 0; int stride = 0; bool refElems = true;
             BNM::IL2CPP::Il2CppClass *elemK = nullptr; };

static bool SeqShape(Il2CppObject *seq, Seq *out) {
    if (!seq || !out) return false;
    auto *sk = Class(seq).GetClass();
    bool isList = sk && sk->name && strstr(sk->name, "List") != nullptr;
    Il2CppObject *arr = seq;
    size_t count;
    if (isList) {
        arr = *(Il2CppObject **)((uint8_t *)seq + 0x10);
        count = (size_t)*(int *)((uint8_t *)seq + 0x18);
    } else {
        count = *(uintptr_t *)((uint8_t *)seq + 0x18);
    }
    if (!arr) return false;
    auto *ak = Class((Il2CppObject *)arr).GetClass();
    auto *ek = ak ? ak->element_class : nullptr;
    if (!ek) return false;
    bool isValue = ek->byval_arg.type == BNM::IL2CPP::IL2CPP_TYPE_VALUETYPE;
    int stride = (int)ak->element_size;
    if (stride <= 0) stride = isValue ? (int)ek->instance_size : 8;
    if (stride <= 0) return false;
    out->base = (uint8_t *)arr + 0x20;
    out->n = count;
    out->stride = stride;
    out->refElems = !isValue;
    out->elemK = ek;
    return true;
}

static bool SeqSane(const Seq &s) {
    return s.n > 0 && s.n <= 100000 && s.stride > 0 && s.stride <= 4096;
}

static bool AddKey(const char *key) {
    if (!key || !key[0] || strlen(key) >= kKeyLen) return false;
    for (int i = 0; i < g_keys.n; i++)
        if (strcmp(g_keys.keys[i], key) == 0) return false;
    if (g_keys.n >= kMaxKeys) return false;
    snprintf(g_keys.keys[g_keys.n], kKeyLen, "%s", key);
    g_keys.n++;
    return true;
}

static int LoadFromLoader() {
    Il2CppObject *mgr = ItemMgr();
    if (!mgr || !G.loadableKeys.GetInfo()) return 0;
    Il2CppObject *lst = nullptr;
    try { lst = G.loadableKeys[mgr](); } catch (...) { return 0; }
    Seq s{};
    if (!SeqShape(lst, &s) || !SeqSane(s) || s.elemK != G.stringClass) return 0;
    int added = 0;
    for (size_t i = 0; i < s.n; i++) {
        auto *str = *(BNM::Structures::Mono::String **)(s.base + i * (size_t)s.stride);
        if (!str || str->length == 0 || str->length > 64) continue;
        try { if (AddKey(str->str().c_str())) added++; } catch (...) {}
    }
    ym::log("items: loader n=%zu added=%d", s.n, added);
    return added;
}

static int ScanRoomKeys() {
    if (!G.tItem || !G.itemKey.IsValid() || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return 0;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    int added = 0;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!it || !G.alive((Il2CppObject *)it)) continue;
        try {
            auto *k = G.itemKey[it]();
            if (k && k->length > 0 && k->length <= 64 && AddKey(k->str().c_str())) added++;
        } catch (...) {}
    }
    ym::log("items: room n=%zu added=%d", n, added);
    return added;
}

static void LoadAllItems() {
    int a = LoadFromLoader();
    int b = ScanRoomKeys();
    ym::log("items: total=%d loader=%d room=%d", g_keys.n, a, b);
}

static bool KeyFromHand(char *out, size_t outSize) {
    Il2CppObject *hand = g_leftHand.get();
    if (!hand || !out || outSize == 0 || !G.tItem || !G.itemKey.IsValid() ||
        !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return false;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    Vector3 hp = G.trGetPosition[hand]();
    Il2CppObject *best = nullptr;
    float bestD = 0.16f;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!it || !G.alive((Il2CppObject *)it)) continue;
        if (G.itemHeld.IsValid() && !G.itemHeld[it]()) continue;
        Vector3 d = G.trGetPosition[G.compTransform[it]()]() - hp;
        float dd = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dd < bestD) { bestD = dd; best = it; }
    }
    auto *key = best ? G.itemKey[best]() : nullptr;
    if (!key) return false;
    snprintf(out, outSize, "%s", key->str().c_str());
    AddKey(out);
    return true;
}

static char g_spawnKey[kKeyLen];

static bool PrepareSpawnKey() {
    if (g_sel >= 0 && g_sel < g_keys.n) {
        snprintf(g_spawnKey, sizeof g_spawnKey, "%s", g_keys.keys[g_sel]);
        return true;
    }
    return KeyFromHand(g_spawnKey, sizeof g_spawnKey);
}

struct Pending { char key[kKeyLen]; Vector3 pos, vel; bool anchored; };
static std::vector<Pending> g_queue;
static double g_nextBatch = 0, g_nextItem = 0;

static bool QueueItem(const char *key, Vector3 pos, Vector3 vel, bool anchored) {
    if (!key || !key[0] || g_queue.size() >= 32) return false;
    Pending p{};
    snprintf(p.key, sizeof p.key, "%s", key);
    p.pos = pos;
    p.vel = vel;
    p.anchored = anchored;
    g_queue.push_back(p);
    return true;
}

static void QueueTick() {
    for (int sent = 0; sent < 2 && !g_queue.empty(); sent++) {
        Pending p = g_queue.front();
        g_queue.erase(g_queue.begin());
        bool ok = SendItemWithParams(p.key, p.pos, Quaternion(0, 0, 0, 1), p.vel, p.anchored, nullptr, 0);
        if (!ok) { g_queue.clear(); ym::log("items: send failed %s", p.key); }
    }
}

static void SpawnAtHand() {
    if (g_time < g_nextItem || g_queue.size() >= 32) return;
    if (!PrepareSpawnKey()) return;
    Il2CppObject *hand = g_rightHand.get();
    if (!hand) return;
    g_nextItem = g_time + 0.1;
    QueueItem(g_spawnKey, G.trGetPosition[hand]() + G.trGetForward[hand]() * 0.1f,
              Vector3(0, -2.0f, 0), false);
}

static void SpawnGrid(int count) {
    Il2CppObject *head = g_head.get();
    if (!head || g_time < g_nextBatch) return;
    if (!PrepareSpawnKey()) return;
    g_nextBatch = g_time + 0.1;
    Vector3 f = G.trGetForward[head]();
    f.y = 0;
    float len = sqrtf(f.x * f.x + f.z * f.z);
    if (len < 0.001f) return;
    f = f * (1.0f / len);
    Vector3 r = Vector3(-f.z, 0, f.x);
    Vector3 base = G.trGetPosition[head]() + f * 1.2f + Vector3(0, -0.4f, 0);
    for (int y = 0; y < (count > 9 ? 3 : 1); y++)
        for (int z = 0; z < 3; z++)
            for (int x = 0; x < 3; x++)
                QueueItem(g_spawnKey, base + r * ((x - 1) * 0.45f) + f * (z * 0.45f) + Vector3(0, y * 0.45f, 0),
                          Vector3(0, 0, 0), true);
}

static void DumpKeys() {
    const char *dir = ym::api() ? ym::api()->modsDir : nullptr;
    if (!dir) return;
    char path[600];
    snprintf(path, sizeof path, "%s/item_keys.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < g_keys.n; i++) fprintf(f, "%s\n", g_keys.keys[i]);
    fclose(f);
    ym::log("items: dumped %d", g_keys.n);
}
