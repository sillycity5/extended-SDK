static bool SendItemWithParams(const char *key, Vector3 pos, Quaternion rot, Vector3 vel, bool anchored,
                               const char *const *params, int nParams, char *idOut = nullptr) {
    auto *mi = G.rpcCreateItem;
    Il2CppObject *nm = G.netLocal.IsValid() ? G.netLocal() : nullptr;
    if (!mi || !nm || !G.pvRpc.IsValid() || !G.arrayNew || !G.valueBox || !G.tPhotonView) return false;
    Il2CppObject *go = G.compGameObject[nm]();
    Il2CppObject *pv = go ? G.goGetComponent[go]((Il2CppObject *)G.tPhotonView) : nullptr;
    if (!pv) return false;

    int count = mi->parameters_count;
    if (count > 0 && TypeName(mi->parameters[count - 1]) == "Photon.Pun.PhotonMessageInfo") count--;

    auto *args = (Il2CppObject *)G.arrayNew(G.objectClass, count);
    if (!args) return false;

    char id[8];
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 4; i++) id[i] = kChars[(int)(Rand01() * 35.99f)];
    id[4] = 0;
    if (idOut) memcpy(idOut, id, 5);

    int strings = 0, vectors = 0, valuetypes = 0;
    for (int i = 0; i < count; i++) {
        std::string tn = TypeName(mi->parameters[i]);
        Class pc(mi->parameters[i]);
        auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
        void *v = nullptr;

        if (tn == "System.String") {
            if (strings == 0) v = CreateMonoString(id);
            else if (strings == 1) v = CreateMonoString(key);
            strings++;
        } else if (tn == "UnityEngine.Vector3") {
            Vector3 vec = vectors == 0 ? pos : vectors == 1 ? vel : Vector3(0, 0, 0);
            vectors++;
            v = G.valueBox(k, &vec);
        } else if (tn == "UnityEngine.Quaternion") {
            v = G.valueBox(k, &rot);
        } else if (tn == "System.String[]") {
            int n = params && nParams > 0 ? nParams : 0;
            Il2CppObject *arr = (Il2CppObject *)G.arrayNew(G.stringClass, n);
            for (int j = 0; j < n; j++) Put(arr, j, CreateMonoString(params[j] ? params[j] : ""));
            v = arr;
        } else if (k && k->byval_arg.valuetype) {
            uint8_t raw[64] = {};
            raw[0] = valuetypes == 0 ? (anchored ? 1 : 0) : 1;
            valuetypes++;
            auto *boxAs = k->enumtype && k->element_class ? k->element_class : k;
            v = G.valueBox(boxAs, raw);
        }
        Put(args, i, v);
    }

    ym::log("egg: args key=%s count=%d params=%d", key, count, nParams);
    try {
        G.pvRpc[pv](CreateMonoString("RpcCreateItem"), 0, args);
    } catch (BNM::IL2CPP::Il2CppExceptionWrapper &w) {
        auto *ex = w.ex;
        auto *msg = ex ? (BNM::Structures::Mono::String *)ex->message : nullptr;
        ym::log("egg: refused %s", msg ? msg->str().c_str() : "?");
        return false;
    }
    return true;
}

struct EggColor { const char *mob, *primary, *secondary; };

static bool EggColorsFor(const char *mob, const char **primary, const char **secondary) {
    static const EggColor t[] = {
        {"alienEye", "blue_turquoise", "guac"},
        {"alienHopper", "purple", "guac"},
        {"armadillo", "maroon_dark", "mocha_light"},
        {"bitingBat", "brown_dark", "red_dark"},
        {"crab", "red", "yellow_light"},
        {"eggSack", "yellow_light", "purple"},
        {"frogling", "green_light", "yellow_light"},
        {"grazingHog", "blue_turquoise", "olive_light"},
        {"grazingSheep", "white", "blue_light"},
        {"grazingYak", "brown_dark", "brown"},
        {"hermitCrab", "red", "blue_dark"},
        {"horrorBear", "mocha", "blue_dark"},
        {"horrorChild", "blue_dark", "black"},
        {"horrorChimp", "yellow", "red"},
        {"horrorEmpty", "black", "black"},
        {"horrorFather", "black", "red"},
        {"horrorFisherman", "green_dark", "pink"},
        {"horrorFrankenstein", "gray_dark", "blue_turquoise"},
        {"horrorGhost_boy", "gray_cool_light", "blue_light"},
        {"horrorGhost_girl", "gray_cool_light", "pink"},
        {"horrorGhost_pet", "gray_cool_light", "green_light"},
        {"horrorGrandma", "maroon", "orange_light"},
        {"horrorMother", "blue_light", "yellow_light"},
        {"horrorYeepingAngel", "purple_dark", "blue_dark"},
        {"insectExploder", "red", "white"},
        {"insectHatchling", "purple", "purple_dark"},
        {"insectQueen", "purple_dark", "purple"},
        {"insectSoldier", "purple", "periwinkle"},
        {"insectTank", "purple_dark", "gray"},
        {"rat", "red_dark", "brown"},
        {"ratKing", "red_dark", "yellow_light"},
        {"scorpion", "purple_dark", "yellow_light"},
        {"shark", "blue_dark", "gray"},
        {"spittingFrog", "green_light", "green"},
        {"spittingLizard", "red", "yellow"},
        {"spittingSnail", "blue_dark", "cyan"},
        {"wasp", "brown_dark", "yellow"},
        {"wolf", "blue_dark", "red"},
        {"yeti", "blue_light", "blue_dark"},
    };
    if (!mob) return false;
    for (const EggColor &e : t)
        if (strcmp(e.mob, mob) == 0) { *primary = e.primary; *secondary = e.secondary; return true; }
    return false;
}

static bool SendEgg(const char *mob, Vector3 pos) {
    if (!mob || !mob[0]) return false;
    const char *primary = nullptr, *secondary = nullptr;
    bool known = EggColorsFor(mob, &primary, &secondary);
    const char *params[3] = {mob, known ? primary : "white", known ? secondary : "white"};
    bool ok = SendItemWithParams("mobSpawnEgg", pos, Quaternion(0, 0, 0, 1), Vector3(0, 0.4f, 0),
                                 false, params, 3);
    ym::log("egg: spawn %s known=%d ok=%d", mob, known ? 1 : 0, ok ? 1 : 0);
    return ok;
}
