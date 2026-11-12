yeeps 2.32 notes

blaze build, arm64, ndk r27c. everything below was run on device unless it says otherwise.

files:

    mod_basics.cpp    skeleton, lookups, refs
    dump_class.cpp    dumps class signatures to signatures.txt
    egg_spawner.cpp   eggs and the SendItemWithParams base
    items.cpp         item list and spawn queue
    fling_gun.cpp     knockback gun
    mob_keys.txt      39 mobs and their colour pairs

mod_basics first. the rest expect its G struct.

## sdk

smallest thing that loads:

    #include "ymsdk.hpp"
    YEEPS_MOD("My Mod")
    YM_EXPORT int YeepsMod_Init(const YeepsModApi *api) {
        if (!ym::init(api)) return -1;
        ym::onUpdate(OnUpdate);
        ym::onRoomChange(OnRoomChange);
        return 0;
    }

init runs before any scene. register callbacks there, nothing else. all unity work in onUpdate / onRoomChange. no threads.

ym:: has init, api, log, hasRole, onUpdate, onRoomChange. modsDir = mods folder, every dump in here writes there. resolve("name") gives the raw il2cpp fn, handy for il2cpp_array_new, value_box, gchandle_new.

## lookups

one pass on frame 1, into a struct. not in a loop, not per press.

    Class item("", "Item", Image("Assembly-CSharp.dll"));
    Method<Il2CppObject *> key = item.GetMethod("get_itemKey", 0);

game classes: no namespace, empty first arg. unity classes take the real one.

GetMethod = name + argc. same argc twice? Overload by types:

    Overload(go, "GetComponent", {"System.Type"})

IsValid on everything. a method that moved in a patch is just invalid and calling through it gives you a crash with no useful line.

instance = method[obj](args). static = method(args). Vector3 etc pass by value both ways.

## refs

pointer kept across frames can be freed by the gc.

    Ref p;
    p.set(ym::api()->localPlayer());
    if (!p.get() || !G.alive((Il2CppObject *)p.get())) return;

Ref is in mod_basics, 3 gchandle calls. alive = Object.op_Implicit.

local player changes = new room. everything cached is dead. clear, rebuild.

## things that break

signature dumps lie about struct params. SendCreateMobFromSpawnEgg(String, X, bool, bool) reads like X is Vector3Int. it isnt. stripped game struct, same anonymous one in SendCreateMob and OnReceiveCreateMob. a grid cell went in, callee read garbage, process died status=11. if a param is a struct you cant name, dont guess the layout. find another route.

enums in rpc args box as the underlying number, not the enum class. value_box wants the int type or the receiver reads the wrong bytes.

photon appends PhotonMessageInfo itself. build from parameters_count - 1.

arrays of value structs arent pointer arrays. stride = element_size, data at +0x20. read a class pointer out of a struct slot and you get the first field bits. thats why catalogue walking is risky.

CreateMonoString in a loop = gc storm. make once.

catch. one exception out of a callback can take the game down. ym:: catches, catch yours too.

## signatures

member names are encrypted at rest in the apk. strings on global-metadata.dat gives scrambled text. type names and literals are plaintext, members arent. static dumper is useless.

runtime instead. dump_class.cpp does only that:

    Item
       field itemKey : System.String @18
       get_itemKey()
       IsHeldOrAnchoredToAnyPlayer()
       RpcCreateItem(System.String,System.String,...)

put the class in kNames, build, launch, read signatures.txt in mods folder. waits 180 frames, writes once.

Readable drops names outside A-Z a-z 0-9 _ < > backtick, so scrambled ones fall out.

cap the methods. Player has hundreds, first 400 is plenty.

## eggs

egg = normal item, key mobSpawnEgg. mob + 2 colours go in the custom params:

    [mobKey, primaryColourKey, secondaryColourKey]

game patterns look like

    mobSpawnEgg_insectExploder -> mobSpawnEgg ["insectExploder", "red", "white"]

mobSpawnEgg_insectExploder is not in the room loader. send the plain key, mob goes in the params. thats the whole thing.

## rpc slots

RpcCreateItem on local player PhotonView, target 0.

| slot | type | value |
| --- | --- | --- |
| 0 | string | item id, 4 chars, a-z0-9 |
| 1 | string | item key, mobSpawnEgg |
| 2 | enum | 0 loose, 1 pinned |
| 3 | string | null |
| 4 | Vector3 | position |
| 5 | Quaternion | rotation |
| 6 | Vector3 | velocity |
| 7 | Vector3 | spin |
| 8 | string[] | the 3 params |
| 9 | byte | 1 |
| 10 | byte | 1 |
| 11 | string | null |
| 12 | int[] | empty |
| 13 | byte[] | empty |

photon adds PhotonMessageInfo on the end, dont put it in.

slot 2 = anchor. egg wants 0 so it drops and you can pick it up. 1 pins it in the air.

dont hardcode the order. build from the method param list like the code does. new build adds a slot and hardcoded order breaks quietly.

## three calls that kill the process

tested. sigsegv inside il2cpp, exit 11.

    MobManager.SendCreateMobFromSpawnEgg(mobKey, <struct>, bool, bool)
    MobManager.TryCreateMobSpawnEgg(mobKey, pos, Item, GameObject)
    MapManager.TryCreateMapObject(...)

TryCreateMobSpawnEgg wants real Item + GameObject. nulls fault it.

so dont hatch the mob. spawn the egg item, throw it.

## egg code

egg_spawner.cpp, 3 pieces. SendItemWithParams = starter SendItem + string[] slot + anchored. EggColorsFor = 39 mobs. SendEgg = mob in, egg out.

drop after Put and CreateMonoString, then

    Vector3 at = G.trGetPosition[hand]() + G.trGetForward[hand]() * 0.6f;
    SendEgg("insectExploder", at);

already got a SendItem? copy just the System.String[] branch into yours, skip SendItemWithParams.

colour keys are the game names, same ones block patterns use. unknown mob -> white / white. plain looking, works.

## mob keys

mob_keys.txt, tab separated: mob, primary, secondary.

live version: MobManager.mobDatas, 63 entries, stride 32, key String at +16. read once on room load.

## egg logs

    egg: args key=mobSpawnEgg count=13 params=3
    egg: spawn insectExploder known=1 ok=1

13 right. 14 = PhotonMessageInfo went in, room drops the item.

ok=1 only means the rpc left without throwing. not proof the egg exists. look on the ground.

## items

same message. only slot 8 differs. items get empty string[], eggs get the 3 params.

slot 2 still anchor.

0 loose, drops, pickup-able.
1 pinned, stays put.

velocity only matters loose. (0,-2,0) drops at your feet. (0,0,0) + anchor 1 = placed block.

## item ids

4 chars a-z0-9, unique in the room. plain counter or rand collides when 2 people run the mod.

menu mixes userID in:

    mix = fnv1a(userID)
    mix ^= clock_ms
    x4: xorshift, mix += ++counter, mix % 36

doesnt have to be that. has to have the userID. 2 clients seeding off the clock = duplicate ids and one item quietly vanishing.

## queue

never straight from the trigger.

2 a frame.
32 waiting, then stop.
0.1s between presses.
27 a batch, 3s after.

QueueTick does the first 3. press -> QueueItem, tick drains. send throws = clear, dont retry into a dead room.

## load all items

2 reads, both safe.

ItemManager.loadableItemKeys, List<String>. game own list of what it will load, so anything in it can be created. read once.

room scan. FindObjectsOfType(Item), get_itemKey() each. catches keys the loader misses, costs one scene search.

LoadFromLoader + ScanRoomKeys in items.cpp, LoadAllItems runs both and logs counts.

dont walk ItemManager.itemDatas. array of ItemData value structs, pulling a String out of one faulted inside il2cpp on this build.

keys are case sensitive. baseballBat != BaseballBat. key the room doesnt have drops with no error.

## key list

no key file in here on purpose. baked lists go stale next time an item is added and then it gets reported as broken.

DumpKeys writes item_keys.txt next to the .so, one per line, order the game gave. ship that or paste it into your own array.

picker caps at 512. game list is longer than a page, so cap and page it, dont draw 500 buttons.

## what to spawn

PrepareSpawnKey: picker selection if there is one, else left hand item.

KeyFromHand = nearest held Item within 0.4m of the left hand transform. radius matters. too small misses small items, too big grabs the block behind you.

## item code

items.cpp. needs SendItemWithParams from the egg file, plus g_time, g_leftHand, g_rightHand, g_head from your own mod.

lookups it wants:

    Field<Il2CppObject *> loadableKeys;      ItemManager.loadableItemKeys
    Method<Il2CppObject *> itemKey;          Item.get_itemKey
    Method<bool> itemHeld;                   Item.IsHeldOrAnchoredToAnyPlayer
    BNM::MonoType *tItem;                    for the scene search
    Method<Il2CppObject *> findScene;        Object.FindObjectsOfType(Type)
    Method<Il2CppObject *> findSceneByType;  Type overload
    Method<Il2CppObject *> compTransform;
    Method<Vector3> trGetPosition, trGetForward
    Method<bool> alive

plus whatever gets the ItemManager singleton. without it ScanRoomKeys still fills the list.

## item logs

    items: loader n=533 added=533
    items: room n=41 added=6
    items: total=539 loader=533 room=6

533 = size of the 2.32 list. n=0 means the field lookup failed, not an empty list.

spawns log nothing on success. watch the ground.

## fling gun

not a projectile. nothing spawns, nothing to hit. baton knockback aimed at a player.

SendApplyPlayerKnockback(userID, force, source, String, Int32, Byte). source 3 = KnockbackSource.baton, same push a baton hit sends, so clients already handle it.

3 ways in, tried in order:

Player.SendApplyKnockback(force, 3) on the target. per player, runs on their client. usually the one that lands.
NetworkMessenger.SendApplyPlayerKnockback, 6 args. room wide.
raw rpc, if a patch changed the wrapper.

raw slots:

| slot | type | value |
| --- | --- | --- |
| 0 | string | target userID |
| 1 | Vector3 | force |
| 2 | int | 3 (baton) |
| 3 | string | empty |
| 4 | int | 0 |
| 5 | byte | 0 |

photon adds msgInfo again. 6, not 7.

## ten pushes

one knockback gets eaten. their movement, their client correcting position, frame timing, and they dont move.

so a press sends 10 in the same frame, y stepped 0.35, then 0.1s cooldown. fans the arc instead of stacking one stuck frame.

10 clears a player without flooding. mass fling uses 4 each, full room multiplies it.

## aim

FindObjectsOfType(Player) when you start aiming, keep the list. not per frame.

skip yourself, skip dead. GetBodyPosition minus hand pos, then:

along (dot with aim dir) 0.3..60m
off (distance to ray) under 1.75m

closest wins. 1.75 is wide on purpose, a body isnt a point and a tight cone feels broken.

## force

    force = dir * speed
    force.y += 4.0

12, 20, 32. the 4 of lift turns a shove into an arc. without it they slide and stop.

## fling code

fling_gun.cpp. uses g_time, drop the declaration if you already have one.

lookups:

    Method<void> netKnockback;          SendApplyPlayerKnockback, 6 args
    Method<void> netKnockback3;         same name, 3 args, older builds
    Method<void> plSendKnockback;       Player.SendApplyKnockback, 2 args
    Method<Il2CppObject *> netLocal;    NetworkMessenger.get_local
    Method<Il2CppObject *> playerUserID;
    Method<Vector3> playerBodyPos;      Player.GetBodyPosition
    BNM::MonoType *tPlayer;
    Method<Il2CppObject *> findScene;
    BNM::IL2CPP::MethodInfo *rpcKnockback;

plus Put, CreateMonoString, pvRpc, arrayNew, valueBox, objectClass, intClass, vector3Class, byteClass, tPhotonView and the transform getters. Put and CreateMonoString come from the egg file.

update:

    if (f_flingGun && triggerPressed) {
        if (!g_aiming) { RefreshTargets(true); g_aiming = true; }
        FlingTarget();
    }

clear g_aiming on release. people leave and join, stale list points at dead objects.

## fling logs

    fling power 1
    fling: no route
    knockback error ...
    push error ...

fling: no route = every path failed. lookup problem, not aim.

nothing logs on a good send, so watch the player. if they dont move the target object probably wasnt theirs. falling through to the raw rpc is the tell, and that one is room wide so it should still land.

## build

    export ANDROID_NDK=/path/to/android-ndk-r27c
    ./build.sh

copy out/<your-mod>.so into Android/data/com.TrassGames.Yeeps/files/mods/ and restart yeeps.
