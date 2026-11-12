static const float kFlingSpeeds[] = {12.0f, 20.0f, 32.0f};
static int g_flingSpeed = 1;
static double g_nextFling = 0;
static double g_time = 0;

static constexpr int kMaxTargets = 16;
static Ref g_targets[kMaxTargets];
static int g_targetCount = 0;

static bool AlivePlayer(Il2CppObject *o) { return o && G.alive((Il2CppObject *)o); }

static void ClearTargets() {
    for (int i = 0; i < g_targetCount; i++) g_targets[i].clear();
    g_targetCount = 0;
}

static void RefreshTargets(bool quiet) {
    ClearTargets();
    if (!G.tPlayer || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tPlayer)
                                                  : G.findSceneByType((Il2CppObject *)G.tPlayer, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    Il2CppObject *local = g_player.get();
    for (size_t i = 0; i < n && g_targetCount < kMaxTargets; i++) {
        Il2CppObject *p = ((Il2CppObject **)(arr + 0x20))[i];
        if (!p || p == local || !AlivePlayer(p)) continue;
        g_targets[g_targetCount++].set(p);
    }
    if (!quiet) ym::log("%d players in the room", g_targetCount);
}

static float VecLength(Vector3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

static Vector3 BodyOf(Il2CppObject *p) {
    if (G.playerBodyPos.IsValid()) return G.playerBodyPos[p]();
    if (auto *t = G.compTransform[p]()) return G.trGetPosition[t]();
    return Vector3(0, 0, 0);
}

static Il2CppObject *AimTarget(Vector3 start, Vector3 dir, Vector3 *posOut = nullptr) {
    Il2CppObject *best = nullptr;
    float bestOff = 1.75f;
    for (int i = 0; i < g_targetCount; i++) {
        Il2CppObject *p = g_targets[i].get();
        if (!AlivePlayer(p)) continue;
        Vector3 at = BodyOf(p);
        Vector3 d = at - start;
        float along = d.x * dir.x + d.y * dir.y + d.z * dir.z;
        if (along < 0.3f || along > 60.0f) continue;
        Vector3 off = d - dir * along;
        float dist = VecLength(off);
        if (dist < bestOff) { bestOff = dist; best = p; if (posOut) *posOut = at; }
    }
    return best;
}

static bool SendKnockback(Il2CppObject *userID, Vector3 force) {
    Il2CppObject *nm = G.netLocal.IsValid() ? G.netLocal() : nullptr;
    if (!nm || !userID) return false;
    if (G.netKnockback.IsValid()) {
        try {
            G.netKnockback[nm](userID, force, (int32_t)3, CreateMonoString(""), (int32_t)0, (uint8_t)0);
            return true;
        } catch (BNM::IL2CPP::Il2CppExceptionWrapper &w) {
            auto *msg = w.ex && w.ex->message ? (BNM::Structures::Mono::String *)w.ex->message : nullptr;
            ym::log("knockback error %s", msg ? msg->str().c_str() : "?");
        }
    } else if (G.netKnockback3.IsValid()) {
        try { G.netKnockback3[nm](userID, force, (int32_t)3); return true; } catch (...) {}
    }
    if (!G.rpcKnockback || !G.pvRpc.IsValid() || !G.arrayNew || !G.valueBox || !G.objectClass || !G.intClass ||
        !G.vector3Class || !G.byteClass || !G.tPhotonView)
        return false;
    Il2CppObject *go = G.compGameObject[nm]();
    Il2CppObject *pv = go ? G.goGetComponent[go]((Il2CppObject *)G.tPhotonView) : nullptr;
    if (!pv) return false;
    auto *args = (Il2CppObject *)G.arrayNew(G.objectClass, 6);
    if (!args) return false;
    int32_t source = 3, zero = 0;
    uint8_t zb = 0;
    Put(args, 0, userID);
    Put(args, 1, G.valueBox(G.vector3Class, &force));
    Put(args, 2, G.valueBox(G.intClass, &source));
    Put(args, 3, CreateMonoString(""));
    Put(args, 4, G.valueBox(G.intClass, &zero));
    Put(args, 5, G.valueBox(G.byteClass, &zb));
    try {
        G.pvRpc[pv](CreateMonoString("RpcApplyPlayerKnockback"), 0, args);
        return true;
    } catch (...) {
        return false;
    }
}

static bool SendKnockbackTo(Il2CppObject *target, Il2CppObject *userID, Vector3 force) {
    if (G.plSendKnockback.IsValid() && target) {
        try {
            G.plSendKnockback[target](force, (int32_t)3);
            return true;
        } catch (BNM::IL2CPP::Il2CppExceptionWrapper &w) {
            auto *msg = w.ex && w.ex->message ? (BNM::Structures::Mono::String *)w.ex->message : nullptr;
            ym::log("push error %s", msg ? msg->str().c_str() : "?");
        }
    }
    return SendKnockback(userID, force);
}

static void FlingTarget() {
    if (g_time < g_nextFling) return;
    Il2CppObject *hand = g_rightHand.get();
    if (!hand || !G.playerUserID.IsValid()) return;
    Vector3 start = G.trGetPosition[hand](), dir = G.trGetForward[hand]();
    Il2CppObject *target = AimTarget(start, dir, nullptr);
    if (!target) return;
    auto *uid = G.playerUserID[target]();
    if (!uid) return;
    Vector3 force = dir * kFlingSpeeds[g_flingSpeed];
    force.y += 4.0f;
    g_nextFling = g_time + 0.1;
    bool any = false;
    for (int i = 0; i < 10; i++) {
        Vector3 f = force;
        f.y += (float)i * 0.35f;
        if (SendKnockbackTo(target, (Il2CppObject *)uid, f)) any = true;
    }
    if (!any) ym::log("fling: no route");
}

static void NextFlingPower() {
    g_flingSpeed = (g_flingSpeed + 1) % 3;
    ym::log("fling power %d", (int)kFlingSpeeds[g_flingSpeed]);
}
