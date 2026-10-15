// CustomCamoDev - in-game test of the Custom Camo plugin. Not shipped; installed only for a run.
// Loads the team room, logs the JPC plate carrier's colour options, puts the JPC on the player in the
// custom camo (then Multicam and Flecktarn to compare) through the game's own gear manager, and
// renders the player with a scene capture of its own, exported as PNG to [Test] ShotDir - the game's
// own rendering, independent of the headset.
#include <GML/GML.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>

using namespace gml;

GML_PLUGIN("com.remarrow.customcamodev", "CustomCamoDev", "0.1.0", {{"com.remarrow.customcamo", "0.1.0", GML_DEPENDENCY_SOFT}});

struct Vec { double x, y, z; };
struct Rot { double pitch, yaw, roll; };

static std::string s_shotDir;
static int s_step = -1;
static ULONGLONG s_at = 0;
static Object s_gear, s_jpc, s_rt, s_cap;
static uint8_t s_cat = 0;

static std::string NameStr(uint64_t n) { char b[128] = ""; API->NameToString(n, b, sizeof b); return b; }
static std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }

static Object Pawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}

static void LogOptions(Object item) {
    GFProperty* prop = API->FindProperty(item.Struct(), "Colors");
    std::string keys;
    int n = API->MapForEach(item.PropPtr("Colors"), prop, [](void* u, void* key, void*) -> int {
        *(std::string*)u += NameStr(*(uint64_t*)key) + ", ";
        return 0;
    }, &keys);
    Log("OPTIONS {} ({}): {}", item.Name(), n, keys);
}

static void Apply(const char* color) {
    Params a(s_gear, "ApplyColor");
    a.Set("Category", s_cat).Set("Color", API->MakeName(color)).Call();
    Log("APPLY {} -> {}", s_jpc.Name(), color);
}

static void EnsureCapture() {
    if (s_rt) return;
    Params rt(Lib("KismetRenderingLibrary"), "CreateRenderTarget2D");
    rt.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Width", (int32_t)1024).Set("Height", (int32_t)1024)
        .Set("Format", (uint8_t)2).Call();  // RTF_RGBA8
    s_rt = rt.Return<GUObject*>();
    Params mk(Lib("KismetMathLibrary"), "MakeTransform");
    mk.Set("Scale", Vec{1, 1, 1}).Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &xi);
    Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
    b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", FindClass("SceneCapture2D")).Set("CollisionHandlingOverride", (uint8_t)1);
    std::memcpy(b.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
    b.Call();
    Object actor = b.Return<GUObject*>();
    Params f(Lib("GameplayStatics"), "FinishSpawningActor");
    f.SetObj("Actor", actor);
    std::memcpy(f.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
    f.Call();
    s_cap = actor.GetObj("CaptureComponent2D");
    s_cap.Set<GUObject*>("TextureTarget", s_rt.ptr);
    s_cap.Set("CaptureSource", (uint8_t)7);  // SCS_BaseColor: the albedo, no lighting - the camo as painted
    s_cap.SetBool("bCaptureEveryFrame", false);
    s_cap.Set("FOVAngle", 50.0f);
}

// The whole player from the front, the side and above (the VR body slumps without a tracked headset,
// so one of these shows the vest), each into the render target and out as <file>_<view>.png.
static void Capture(const std::string& file) {
    EnsureCapture();
    Object pawn = Pawn();
    // The body's chest: spine_03 of the pawn's skeletal mesh that has the mannequin skeleton (the visible
    // outfit follows its pose).
    Vec o{};
    bool found = false;
    Params gc(pawn, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("SkeletalMeshComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num && !found; i++) {
        Object c(((GUObject**)arr->Data)[i]);
        Params bi(c, "GetBoneIndex");
        *(uint64_t*)bi.Ptr("BoneName") = API->MakeName("spine_03");
        bi.Call();
        if (bi.Return<int32_t>() < 0) continue;
        Params sl(c, "GetSocketLocation");
        sl.Set("InSocketName", API->MakeName("spine_03")).Call();
        o = sl.Return<Vec>();
        found = true;
    }
    Vec ext{0, 0, 0};
    double reach = 160;
    Params ar(pawn, "K2_GetActorRotation");
    ar.Call();
    double yaw = ar.Return<Rot>().yaw * 3.14159265358979 / 180;
    struct View { const char* name; Vec at; } views[] = {
        {"front", {o.x + std::cos(yaw) * reach, o.y + std::sin(yaw) * reach, o.z + reach * 0.3}},
        {"side", {o.x - std::sin(yaw) * reach, o.y + std::cos(yaw) * reach, o.z + reach * 0.3}},
        {"above", {o.x + std::cos(yaw) * reach * 0.2, o.y + std::sin(yaw) * reach * 0.2, o.z + reach}},
        {"back", {o.x - std::cos(yaw) * reach, o.y - std::sin(yaw) * reach, o.z + reach * 0.3}},
    };
    for (auto& v : views) {
        Params look(Lib("KismetMathLibrary"), "FindLookAtRotation");
        look.Set("Start", v.at).Set("Target", o).Call();
        Params mv(Object(API->GetOuter(s_cap.ptr)), "K2_SetActorLocationAndRotation");
        mv.Set("NewLocation", v.at).Set("NewRotation", look.Return<Rot>()).Set("bSweep", false).Set("bTeleport", true).Call();
        Params cs(s_cap, "CaptureScene");
        cs.Call();
        Params ex(Lib("KismetRenderingLibrary"), "ExportRenderTarget");
        ex.SetObj("WorldContextObject", Object(API->WorldContext())).SetObj("TextureRenderTarget", s_rt)
            .SetString("FilePath", W(s_shotDir)).SetString("Filename", W(file + "_" + v.name + ".png")).Call();
    }
    Log("SHOT {} (player bounds at {:.0f} {:.0f} {:.0f}, extent {:.0f} {:.0f} {:.0f})", file, o.x, o.y, o.z, ext.x, ext.y, ext.z);
}

static void Step() {
    if (GetTickCount64() < s_at) return;
    std::string camo = "Custom Camo";
    for (auto& s : Settings("com.remarrow.customcamo")) if (s.key == std::string("Name")) camo = s.Value();
    switch (s_step) {
        case 0: {
            s_jpc = FindObject("/Game/Characters/Data/DA_PlateCarrier_JPC.DA_PlateCarrier_JPC");
            Object pawn = Pawn();
            for (auto& g : FindAllOf("BP_GearManagerComponent_C"))
                if (API->GetOuter(g.ptr) == pawn.ptr) s_gear = g;
            if (!s_jpc || !s_gear) { Error("missing pieces: jpc {} gear {}", s_jpc.Name(), s_gear.Name()); s_step = 99; return; }
            LogOptions(s_jpc);
            s_cat = s_jpc.Get<uint8_t>("Category");
            struct { GUObject* data; uint64_t color; } item{s_jpc.ptr, API->MakeName(camo.c_str())};
            Params so(s_gear, "SetOutfitItem");
            std::memcpy(so.Ptr("GearItem"), &item, sizeof item);
            so.Call();
            Apply(camo.c_str());
            s_step = 1; s_at = GetTickCount64() + 4000;
            return;
        }
        case 1: Capture("vest_custom"); Apply("Multicam"); s_step = 2; s_at = GetTickCount64() + 3000; return;
        case 2: Capture("vest_multicam"); Apply("Flecktarn"); s_step = 3; s_at = GetTickCount64() + 3000; return;
        case 3: Capture("vest_flecktarn"); Apply(camo.c_str()); s_step = 4; s_at = GetTickCount64() + 2000; return;
        case 4: Log("DEV TEST COMPLETE"); s_step = 99; return;
    }
}

GML_AWAKE() {
    s_shotDir = Config.Bind<std::string>("Test", "ShotDir", "", "Where the captures go.").Value();
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void* gm) {
        std::string level = Object((GUObject*)gm).Path();
        if (level.find("/Game/Maps/MainMenu/") == 0) {
            Params p(Lib("GameplayStatics"), "OpenLevel");
            p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("LevelName", API->MakeName("/Game/Maps/TeamRoom/Teamroom")).Set("bAbsolute", true).Call();
        } else if (level.find("Teamroom") != std::string::npos) {
            s_step = 0;
            s_at = GetTickCount64() + 20000;  // let the room and the player's kit load
        }
    });
    On(GML_EVENT_TICK, [](void*) { if (s_step >= 0 && s_step < 99) Step(); });
    return 0;
}
