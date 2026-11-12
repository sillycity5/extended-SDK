#include "ymsdk.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace BNM;
using namespace BNM::Structures::Unity;
using BNM::IL2CPP::Il2CppObject;

YEEPS_MOD("My Mod")

static void *(*p_handleNew)(void *, bool) = nullptr;
static void *(*p_handleTarget)(void *) = nullptr;
static void (*p_handleFree)(void *) = nullptr;

struct Ref {
    void *h = nullptr;
    void set(Il2CppObject *o) { clear(); if (o && p_handleNew) h = p_handleNew(o, false); }
    Il2CppObject *get() const { return h && p_handleTarget ? (Il2CppObject *)p_handleTarget(h) : nullptr; }
    void clear() { if (h && p_handleFree) p_handleFree(h); h = nullptr; }
};

static double g_time = 0;
static bool g_aiming = false;
static Ref g_player, g_leftHand, g_rightHand, g_head;

struct XRDevice { uint64_t id; bool initialized; };

static struct {
    bool ok = false;
    Method<void> pvRpc, netKnockback, netKnockback3, plSendKnockback;
    Method<Il2CppObject *> netLocal, playerUserID, findScene, findSceneByType, compGameObject,
                           goGetComponent, itemKey, compTransform;
    Method<Vector3> playerBodyPos, trGetPosition, trGetForward;
    Method<bool> alive, itemHeld, tryGetBool;
    Method<XRDevice> deviceAtNode;
    Field<Il2CppObject *> loadableKeys;
    Field<BNM::Structures::Mono::String *> usagePrimary, usageSecondary, usageGrip, usageTrigger;
    BNM::MonoType *tItem = nullptr, *tPlayer = nullptr, *tPhotonView = nullptr;
    BNM::IL2CPP::MethodInfo *rpcCreateItem = nullptr, *rpcKnockback = nullptr;
    void *(*arrayNew)(void *, uintptr_t) = nullptr;
    void *(*valueBox)(void *, void *) = nullptr;
    BNM::IL2CPP::Il2CppClass *objectClass = nullptr, *stringClass = nullptr, *intClass = nullptr,
                *byteClass = nullptr, *vector3Class = nullptr;
} G;

static Il2CppObject *CreateMonoString(const char *s) {
    static Method<Il2CppObject *> create;
    if (!create.IsValid()) create = Class("System", "String").GetMethod("CreateString", 1);
    if (!create.IsValid()) return nullptr;
    auto *str = (BNM::Structures::Mono::String *)create(s);
    return (Il2CppObject *)str;
}

static void Put(Il2CppObject *arr, size_t i, void *v) { ((void **)((uint8_t *)arr + 0x20))[i] = v; }

static float Rand01() {
    static uint32_t s = 0x9e3779b9u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float)(s & 0xffffff) / 16777216.0f;
}

static std::string TypeName(const BNM::IL2CPP::Il2CppType *t) {
    Class c(t);
    if (!c.IsValid() || !c.GetClass() || !c.GetClass()->name) return "?";
    auto *k = c.GetClass();
    return (k->namespaze && k->namespaze[0]) ? std::string(k->namespaze) + "." + k->name : k->name;
}

static MethodBase Overload(Class c, const char *name, std::vector<std::string> types) {
    for (auto &m : c.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name || strcmp(mi->name, name) != 0 || mi->parameters_count != types.size()) continue;
        bool ok = true;
        for (size_t i = 0; i < types.size() && ok; i++) ok = TypeName(mi->parameters[i]) == types[i];
        if (ok) return m;
    }
    return {};
}

static void Lookups() {
    p_handleNew = (decltype(p_handleNew))ym::api()->resolve("il2cpp_gchandle_new");
    p_handleTarget = (decltype(p_handleTarget))ym::api()->resolve("il2cpp_gchandle_get_target");
    p_handleFree = (decltype(p_handleFree))ym::api()->resolve("il2cpp_gchandle_free");
    G.arrayNew = (decltype(G.arrayNew))ym::api()->resolve("il2cpp_array_new");
    G.valueBox = (decltype(G.valueBox))ym::api()->resolve("il2cpp_value_box");

    Class obj("UnityEngine", "Object");
    G.findScene = obj.GetMethod("FindObjectsOfType", 1);
    G.findSceneByType = Overload(obj, "FindObjectsOfType", {"System.Type"});
    Class go("UnityEngine", "GameObject");
    G.compGameObject = go.GetMethod("GetComponent", 0);
    G.goGetComponent = Overload(go, "GetComponent", {"System.Type"});
    Class tr("UnityEngine", "Transform");
    G.trGetPosition = tr.GetMethod("get_position", 0);
    G.trGetForward = tr.GetMethod("get_forward", 0);
    Class cobj("UnityEngine", "Component");
    G.compTransform = cobj.GetMethod("get_transform", 0);
    G.alive = obj.GetMethod("op_Implicit", 1);
    G.objectClass = obj.GetClass();
    G.stringClass = Class("System", "String").GetClass();

    Class item("", "Item", Image("Assembly-CSharp.dll"));
    if (item.IsValid()) {
        G.tItem = item.GetMonoType();
        G.itemKey = item.GetMethod("get_itemKey", 0);
        G.itemHeld = item.GetMethod("IsHeldOrAnchoredToAnyPlayer", 0);
    }
    Class im("", "ItemManager", Image("Assembly-CSharp.dll"));
    if (im.IsValid()) {
        G.loadableKeys = im.GetField("loadableItemKeys");
    }
    Class net("", "NetworkMessenger", Image("Assembly-CSharp.dll"));
    if (net.IsValid()) {
        G.netLocal = net.GetMethod("get_local", 0);
        G.netKnockback = net.GetMethod("SendApplyPlayerKnockback", 6);
        if (!G.netKnockback.IsValid()) G.netKnockback3 = net.GetMethod("SendApplyPlayerKnockback", 3);
        for (auto &m : net.GetMethods(false)) {
            auto *mi = m.GetInfo();
            if (!mi || !mi->name) continue;
            if (strcmp(mi->name, "RpcCreateItem") == 0) G.rpcCreateItem = mi;
            if (strcmp(mi->name, "RpcApplyPlayerKnockback") == 0) G.rpcKnockback = mi;
        }
    }
    Class p("", "Player", Image("Assembly-CSharp.dll"));
    if (p.IsValid()) {
        G.tPlayer = p.GetMonoType();
        G.playerUserID = p.GetMethod("get_userID", 0);
        G.playerBodyPos = p.GetMethod("GetBodyPosition", 0);
        G.plSendKnockback = p.GetMethod("SendApplyKnockback", 2);
    }
    Class pv("Photon.Pun", "PhotonView");
    if (pv.IsValid()) {
        G.tPhotonView = pv.GetMonoType();
        G.pvRpc = Overload(pv, "RPC", {"System.String", "Photon.Pun.RpcTarget", "System.Object[]"});
    }
    Class xr("UnityEngine.XR", "InputDevices");
    if (xr.IsValid()) {
        G.deviceAtNode = xr.GetMethod("GetDeviceAtXRNode", 1);
        Class dev("UnityEngine.XR", "InputDevice");
        G.tryGetBool = dev.GetMethod("TryGetFeatureValue_bool", 3);
    }
    G.ok = G.rpcCreateItem && G.pvRpc.IsValid() && G.objectClass && G.arrayNew && G.valueBox;
    ym::log("lookups ok=%d", G.ok ? 1 : 0);
}

static void RefreshPlayer() {
    Il2CppObject *p = (Il2CppObject *)ym::api()->localPlayer();
    if (p == g_player.get()) return;
    g_player.set(p);
    Class mp("", "MasterPlayer", Image("Assembly-CSharp.dll"));
    if (!mp.IsValid() || !p) return;
    Field<Il2CppObject *> lh = mp.GetField("leftHandTransform");
    Field<Il2CppObject *> rh = mp.GetField("rightHandTransform");
    Field<Il2CppObject *> hd = mp.GetField("headTransform");
    g_leftHand.set(lh.IsValid() ? lh[p]() : nullptr);
    g_rightHand.set(rh.IsValid() ? rh[p]() : nullptr);
    g_head.set(hd.IsValid() ? hd[p]() : nullptr);
}

static void OnRoomChange() {
    g_player.clear();
    g_leftHand.clear();
    g_rightHand.clear();
    g_head.clear();
}

static void OnUpdate(float dt) {
    g_time += dt;
    if (!G.ok) { Lookups(); if (!G.ok) return; }
    RefreshPlayer();
    if (!g_player.get()) return;
}

YM_EXPORT int YeepsMod_Init(const YeepsModApi *api) {
    if (!ym::init(api)) return -1;
    ym::onUpdate(OnUpdate);
    ym::onRoomChange(OnRoomChange);
    return 0;
}
