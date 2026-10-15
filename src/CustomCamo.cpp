// Custom Camo - GML plugin (com.remarrow.customcamo).
//
// Adds a camo to the kit station: every kit item that offers camos gets one more option.
//
// How the game does camo (read from its code): the whole outfit is one material that samples a
// texture array, /Game/Characters/Common/Camo, one 2048x2048 slice per camo. BP_GameInstance.Camos
// lists the camo tiles in slice order (23 of them). Each kit item (an OutfitItem data asset) has a
// Colors map: option name -> ColorOptionStruct {Texture, tints A/B/C, overall tint, pattern tiling}.
// Picking an option looks its Texture up in Camos; that index is the slice the material uses.
//
// So a new camo takes three things:
//   1. a 24th slice in the camo array - Paks\zzz_CustomCamo_P is that package rebuilt with the game's
//      23 slices untouched plus ours (build.bat, Utils\UAssetParser camoarray add); GML installs it;
//   2. our tile appended to BP_GameInstance.Camos, so its index is 23 - a runtime texture of the camo
//      (also the swatch the menu shows), built into this DLL;
//   3. an option in each kit item's Colors map - a copy of one of its existing camo options (by default
//      Flecktarn: its tints for the trim and webbing areas suit a woodland camo) pointing at our tile.
//      GML can only read maps, so the entry is added here (see "map insertion").
//
// Settings: GML\config\com.remarrow.customcamo.cfg. Build: build.bat [install].
#include <GML/GML.hpp>
#include <windows.h>
#include <cmath>
#include <set>
#include "version.h"

using namespace gml;

GML_PLUGIN("com.remarrow.customcamo", "Custom Camo", CUSTOMCAMO_VERSION);

static constexpr int kGameCamos = 23;  // the game's camo array slices; ours is slice 23
static ConfigEntry<std::string> s_name, s_copyFrom;
static ConfigEntry<float> s_tiling;
static GUObject* s_tile = nullptr;     // runtime Texture2D of the camo: our entry in the camo list
static bool s_listOk = false;          // our tile is the camo list's slice 23

// ------------------------------------------------------------------ the camo list

static GUObject* Tile() {
    if (s_tile) return s_tile;
    Blob b = Resource("SWATCH");
    if (!b) { Error("the camo picture is missing from the DLL"); return nullptr; }
    s_tile = API->ImportTextureFromMemory(b.data, b.size, "CustomCamo");
    if (!s_tile) Error("could not make the camo texture");
    return s_tile;
}

static void AddToCamoList(Object gi) {
    auto* raw = (GML_TArray*)gi.PropPtr("Camos");
    if (!raw || !Tile()) return;
    TArrayView<GUObject*> list{raw};
    if (list.Contains(s_tile)) {
        s_listOk = list[kGameCamos] == s_tile;
        return;
    }
    if (list.Num() != kGameCamos) {
        Error("the game's camo list has {} entries, not {}: this camo array was built for another version of "
              "the game, so the camo isn't added (its slice wouldn't match)", list.Num(), kGameCamos);
        return;
    }
    s_listOk = list.Add(s_tile);
    Log("camo list: '{}' added as camo {}", s_name.Value(), kGameCamos);
}

// ------------------------------------------------------------------ map insertion
//
// A TMap<FName, FColorOptionStruct> is a TSet of pairs: a sparse array of elements {pair, HashNextId,
// HashIndex} with a bit array of which are in use, plus a bucket array (HashSize buckets) of chain heads.
// Memory layout (UE 5.7, as GML's MapForEach reads it):
//   0x00 element data   0x08 Num   0x0C Max
//   0x10 allocation bits (4 inline uint32)   0x20 their heap pointer (if more)   0x28 NumBits  0x2C MaxBits
//   0x30 FirstFreeIndex   0x34 NumFreeIndices
//   0x38 bucket (inline, HashSize 1)   0x40 buckets (heap)   0x48 HashSize
// Nothing is written unless the map checks out: no free slots, the allocation bits match, and the bucket
// chains reach every element exactly once, each in the bucket it records.
//
// The new element goes at the END OF EVERY bucket chain. A lookup (Map_Find, FindId) hashes the key to a
// bucket and walks that bucket's chain: an existing key is met before our element, ours is met at the
// end of whichever chain its hash picks, and a missing key ends there too. So the game's FName hash never
// has to be known - this build's isn't the textbook one (25 keys' stored buckets fitted no common hash).
// Listing (Map_Keys) walks the elements, not the chains; a rehash by the engine relinks everything with
// its own hash. Only removing our element would leave other chains pointing at it, and the game never
// removes colour options.

template <class T> static T& At(void* p, size_t o) { return *(T*)((uint8_t*)p + o); }

struct MapView {
    uint8_t* map;
    int stride, valueOff, hashNextOff;
    uint8_t* Elem(int i) const { return At<uint8_t*>(map, 0x00) + (size_t)i * stride; }
    int Num() const { return At<int32_t>(map, 0x08); }
    int Max() const { return At<int32_t>(map, 0x0C); }
    uint32_t* Bits() const { uint32_t* h = At<uint32_t*>(map, 0x20); return h ? h : (uint32_t*)(map + 0x10); }
    bool Used(int i) const { return (Bits()[i / 32] >> (i % 32)) & 1; }
    int HashSize() const { return At<int32_t>(map, 0x48); }
    int32_t* Buckets() const { return HashSize() > 1 ? At<int32_t*>(map, 0x40) : (int32_t*)(map + 0x38); }
    uint64_t Key(int i) const { return At<uint64_t>(Elem(i), 0); }
    int32_t& HashNext(int i) const { return At<int32_t>(Elem(i), hashNextOff); }
    int32_t& HashIndex(int i) const { return At<int32_t>(Elem(i), hashNextOff + 4); }
};

// The map as expected? (see above) An empty string if so, else what is off.
static std::string CheckMap(const MapView& m) {
    int num = m.Num(), hs = m.HashSize();
    if (num <= 0 || num > m.Max()) return std::format("{} of {} elements", num, m.Max());
    if (At<int32_t>(m.map, 0x34) != 0) return "it has free slots";
    if (At<int32_t>(m.map, 0x28) != num) return std::format("{} allocation bits for {} elements", At<int32_t>(m.map, 0x28), num);
    if (hs <= 0 || (hs & (hs - 1))) return std::format("hash size {}", hs);
    for (int i = 0; i < num; i++)
        if (!m.Used(i)) return std::format("element {} not in use", i);
    std::vector<int> seen(num, 0);
    int reached = 0;
    for (int b = 0; b < hs; b++)
        for (int e = m.Buckets()[b], guard = 0; e != -1 && guard <= num; e = m.HashNext(e), guard++) {
            if (e < 0 || e >= num || seen[e]++ || m.HashIndex(e) != b) return std::format("bucket {} chain broken at {}", b, e);
            reached++;
        }
    if (reached != num) return std::format("chains reach {} of {} elements", reached, num);
    return "";
}

// Appends key -> a copy of element `from`'s value at the end of every bucket chain; returns the new
// value, or null.
static uint8_t* MapAppend(MapView& m, uint64_t key, int from, int valueSize) {
    int i = m.Num();
    if (i >= At<int32_t>(m.map, 0x2C)) return nullptr;  // no room in the allocation bits
    if (i == m.Max()) {
        int max = m.Max() * 2;
        auto* data = (uint8_t*)API->EngineAlloc((size_t)max * m.stride);
        if (!data) return nullptr;
        std::memcpy(data, m.Elem(0), (size_t)i * m.stride);  // the old block is left behind: a few KB
        At<uint8_t*>(m.map, 0x00) = data;
        At<int32_t>(m.map, 0x0C) = max;
    }
    uint8_t* e = m.Elem(i);
    std::memset(e, 0, m.stride);
    At<uint64_t>(e, 0) = key;
    std::memcpy(e + m.valueOff, m.Elem(from) + m.valueOff, valueSize);
    m.HashNext(i) = -1;
    m.HashIndex(i) = 0;
    for (int b = 0; b < m.HashSize(); b++) {  // every chain's tail now leads to it
        int32_t* link = &m.Buckets()[b];
        while (*link != -1) link = &m.HashNext(*link);
        *link = i;
    }
    m.Bits()[i / 32] |= 1u << (i % 32);
    At<int32_t>(m.map, 0x28) = i + 1;
    At<int32_t>(m.map, 0x08) = i + 1;
    return e + m.valueOff;
}

// ------------------------------------------------------------------ kit items

static std::string NameStr(uint64_t n) {
    char b[128] = "";
    API->NameToString(n, b, sizeof b);
    return b;
}

static int s_items = 0;  // kit items given the option

// One kit item: if it offers camos, add ours as a copy of its `CopyFrom` option (or its first camo).
static void AddOption(Object item) {
    GFProperty* prop = API->FindProperty(item.Struct(), "Colors");
    void* map = item.PropPtr("Colors");
    GML_PropInfo mi{}, vi{};
    if (!prop || !map || !API->GetPropertyInfo(prop, &mi) || !mi.value || !API->GetPropertyInfo(mi.value, &vi) || !vi.structType) return;
    GFProperty* texProp = API->FindProperty(vi.structType, "Texture");
    GFProperty* tilingProp = API->FindProperty(vi.structType, "TileA_PatternTiling");
    GML_PropInfo ti{}, li{};
    if (!texProp || !API->GetPropertyInfo(texProp, &ti)) return;
    bool hasTiling = tilingProp && API->GetPropertyInfo(tilingProp, &li);
    int pairSize = (vi.offset + vi.size + 7) & ~7;  // the value struct holds a pointer: 8-byte aligned
    MapView m{(uint8_t*)map, pairSize + 8, vi.offset, pairSize};
    if (m.Num() == 0) return;
    uint64_t ours = API->MakeName(s_name.Value().c_str());
    for (int i = 0; i < m.Num(); i++)
        if (m.Used(i) && m.Key(i) == ours) return;  // already has it
    static std::set<GUObject*> warned;
    if (std::string why = CheckMap(m); !why.empty()) {
        if (warned.insert(item.ptr).second) Warn("{}: its Colors map isn't as expected ({}) - not touched", item.Name(), why);
        return;
    }

    int from = -1, firstCamo = -1;
    for (int i = 0; i < m.Num(); i++) {
        Object tex(At<GUObject*>(m.Elem(i) + m.valueOff, ti.offset));
        std::string tn = tex ? tex.Name() : "";
        bool camo = tex && tn != "T_MidColor_Tile" && tn != "White";
        if (camo && firstCamo < 0) firstCamo = i;
        if (camo && _stricmp(NameStr(m.Key(i)).c_str(), s_copyFrom.Value().c_str()) == 0) from = i;
    }
    if (firstCamo < 0) return;  // plain colours only: this item doesn't take camo
    if (from < 0) from = firstCamo;
    uint8_t* v = MapAppend(m, ours, from, vi.size);
    if (!v) { Warn("{}: no room to add the option", item.Name()); return; }
    At<GUObject*>(v, ti.offset) = s_tile;
    if (hasTiling && s_tiling.Value() > 0) At<float>(v, li.offset) = s_tiling.Value();
    s_items++;
    Log("{}: '{}' added ({} options), trim tints from '{}'", item.Name(), s_name.Value(), m.Num(), NameStr(m.Key(from)));
}

static void AddEverywhere() {
    if (!s_listOk) {
        if (Object gi = FindFirstOf("BP_GameInstance_C")) AddToCamoList(gi);
        if (!s_listOk) return;
    }
    for (auto& item : FindAllOf("OutfitItem_C")) AddOption(item);
}

GML_AWAKE() {
    s_name = Config.Bind("Camo", "Name", "Custom Camo",
        "The camo's name in the kit station's colour menu (and in saved kits: renaming it makes kits saved\n"
        "with the old name fall back to the item's default colour).");
    s_copyFrom = Config.Bind("Camo", "CopyTintsFrom", "Flecktarn",
        "The existing camo option whose settings are copied for each item: the tints of the item's trim\n"
        "and webbing areas, and the pattern scale. Items without it copy their first camo.");
    s_tiling = Config.Bind("Camo", "Tiling", 0.0f,
        "Pattern scale on the kit (the game's TileA_PatternTiling; its camos use 1). 0 = the copied option's.");
    if (API->size < offsetof(GML_API, ImportTextureFromMemory) + sizeof(void*)) {
        Error("this plugin needs GML 2.2 or newer");
        return 1;
    }
    // The kit items are loaded when the game instance lists its items; check again at every level start
    // and now and then, for any loaded later. An item already given the option is left alone.
    HookAfter("BP_GameInstance_C:GetItems", [](Object, GUFunction*, void*) { AddEverywhere(); });
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void*) { AddEverywhere(); });
    On(GML_EVENT_TICK, [](void*) {
        static ULONGLONG next = 0;
        if (GetTickCount64() < next) return;
        next = GetTickCount64() + 2000;
        AddEverywhere();
    });
    Log("Custom Camo {} - adds '{}' to every kit item that takes camo", CUSTOMCAMO_VERSION, s_name.Value());
    return 0;
}
