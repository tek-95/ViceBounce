#pragma once
// Everything the plugin needs to know about one specific Vice City executable.
//
// ViceBounce patches vtables and call sites in gta-vc.exe, so each supported executable needs its own
// table of addresses plus byte signatures that prove those addresses are what we think they are.
// The tables are verified at startup by VerifyLayout() (GameLayoutCheck.h); if anything differs the
// plugin disables itself instead of patching blindly.
//
// This header deliberately has no dependency on Plugin-SDK so host-side tools and tests can use it.
#include <cstddef>
#include <cstdint>
#include "LightCallsites.h"

namespace vicebounce {

// A transport class's vtable. Slot 12 is PreRender and slot 13 is Render.
struct VehicleHook { uintptr_t vtable, preRender, render; };
// `call rel32` at `address` that must land on `target`.
struct CallCheck { uintptr_t address, target; const char* what; };
// Bytes that must be present at `address` (at most 15; unused tail bytes are ignored).
struct ByteCheck { uintptr_t address; unsigned char size; unsigned char bytes[15]; const char* what; };

template<class T> struct TableRef {
    const T* first; size_t count;
    const T* begin() const { return first; }
    const T* end() const { return first + count; }
    size_t size() const { return count; }
};
template<class T,size_t N> constexpr TableRef<T> Whole(const T (&array)[N]) { return {array,N}; }

struct GameLayout {
    const char* name;
    // Globals the plugin reads.
    uintptr_t modelInfoPtrs;    // CModelInfo::ms_modelInfoPtrs
    uintptr_t replayMode;       // CReplay::Mode (1 = playback)
    uintptr_t cutsceneRunning;  // CCutsceneMgr::ms_running
    uintptr_t cutsceneCount;    // CCutsceneMgr::ms_numCutsceneObjs
    uintptr_t cutsceneActors;   // CCutsceneMgr::ms_pCutsceneObjects
    // Transport vtables whose PreRender/Render slots are hooked.
    VehicleHook automobile, boat, heli, plane, bike;
    // CCutsceneObject vtable: slot 2 deleting destructor, slot 6 DeleteRwObject, slot 13 Render.
    uintptr_t cutsceneVtable, cutsceneDestructor, cutsceneDeleteRw, cutsceneRender;
    // RenderWare functions called directly.
    uintptr_t updateFrame;      // RwFrameUpdateObjects
    uintptr_t getLtm;           // RwFrameGetLTM
    uintptr_t getHierarchy;     // GetAnimHierarchyFromSkinClump(RpClump*) -> RpHAnimHierarchy*
    // Light functions that the transport render code calls (see LightCallsites.h).
    uintptr_t registerCorona;       // CCoronas::RegisterCorona (type overload)
    uintptr_t updateCorona;         // CCoronas::UpdateCoronaCoors
    uintptr_t addPointLight;        // CPointLights::AddLight
    uintptr_t registerBrightLight;  // CBrightLights::RegisterOne
    uintptr_t storeLightShadow;     // CShadows::StoreCarLightShadow
    // Evidence that the addresses above are right for this executable.
    TableRef<CallCheck> calls;
    TableRef<ByteCheck> signatures;
    TableRef<LightCallsite> lightCallsites;
};

// ---------------------------------------------------------------------------------------------
// Steam executable. Values and signatures are exactly those of the original Steam-only release.
// ---------------------------------------------------------------------------------------------
constexpr CallCheck callChecksSteam[]={
    {0x4A5C70,0x4A42D0,"game process event call (CGame::Process)"},
    {0x4E0000,0x4E2630,"cutscene object render base call"},
    {0x4FE0D6,0x4886B0,"ped render / occupant render call"},
};
constexpr ByteCheck byteChecksSteam[]={
    {0x40650D,7,{0xC6,0x05,0xBA,0xFA,0xA0,0,1},"cutscene running flag"},
    {0x517104,6,{0x8B,0x8B,0xF8,0x01,0,0},"ped vehicle animation field"},
    {0x4063B8,6,{0x8B,0x1D,0x7C,0xEC,0xA0,0},"cutscene object count"},
    {0x406402,7,{0x8B,0x0C,0x9D,0x90,0x72,0x93,0},"cutscene object array"},
    {0x406640,6,{0x89,0xAB,0x9C,0x01,0,0},"cutscene attachment field"},
    {0x4FE11D,8,{0xD9,0x06,0x8D,0x50,0x10,0x50,0xD9,0x1A},"weapon matrix copy in CPed::Render"},
    {0x57F080,11,{0x83,0xEC,0x08,0xC7,0x44,0x24,0x04,0,0,0,0},"skin clump hierarchy getter"},
    {0x645320,8,{0x8B,0x44,0x24,0x04,0x8B,0x40,0x08,0xC3},"HAnim matrix array getter"},
    {0x64B9FD,3,{0x8B,0x46,0x14},"geometry vertex count offset"},
    {0x64BA19,3,{0x8B,0x46,0x5C},"geometry morph target offset"},
    {0x5895C0,9,{0x53,0x56,0x89,0xCB,0x57,0x55,0x83,0xEC,0x18},"CAutomobile::Render prologue"},
    {0x4A60A6,5,{0xE8,0x75,0xA7,0x17,0},"drawing event call"},
    {0x4D77FB,7,{0xC6,0x05,0xF6,0xFA,0xA0,0,1},"replay mode flag"},
    {0x643CB0,15,{0x8B,0x44,0x24,0x04,0x33,0xC9,0x8B,0x90,0xA0,0,0,0,0x8A,0x4A,0x03},"RwFrameUpdateObjects"},
    {0x643D30,15,{0x56,0x8B,0x74,0x24,0x08,0x8B,0x86,0xA0,0,0,0,0xF6,0x40,0x03,0x01},"RwFrameGetLTM"},
};
constexpr GameLayout layoutSteam={
    "Steam",
    0x92C4D0,0xA0FAF6,0xA0FABA,0xA0EC7C,0x937290,
    {0x699D90,0x589900,0x5895C0},    // CAutomobile
    {0x69A0B4,0x5A53A0,0x5A4C10},    // CBoat
    {0x69B21C,0x5AF4C0,0x5AEFD0},    // CHeli
    {0x69B3F8,0x5B23C0,0x5B23A0},    // CPlane
    {0x6D6B04,0x60A9B0,0x60A970},    // CBike
    0x690594,0x4E0400,0x4895A0,0x4DFFF0,
    0x643CB0,0x643D30,0x57F080,
    0x542690,0x5422D0,0x5675F0,0x571710,0x56DBC0,
    Whole(callChecksSteam),Whole(byteChecksSteam),Whole(lightCallsitesSteam),
};

// ---------------------------------------------------------------------------------------------
// Retail 1.0 English executable (Plugin-SDK GAME_10EN).
//
// Steam and 1.0 contain the same compiled code, relocated; data addresses are 0xFF8 lower in Steam.
// Each value was found by the same function's identity in the 1.0 file, then confirmed against the
// bytes of a real 1.0 gta-vc.exe and, where Plugin-SDK documents the 1.0 address, against the SDK.
// Run `VerifyExe <path to gta-vc.exe>` (tools/VerifyExe.cpp) to re-check any executable.
// ---------------------------------------------------------------------------------------------
constexpr CallCheck callChecks10EN[]={
    {0x4A5DA0,0x4A4410,"game process event call (CGame::Process)"},
    {0x4E0140,0x4E2770,"cutscene object render base call"},
    {0x4FE216,0x4887D0,"ped render / occupant render call"},
    // The game itself fetches an HAnim hierarchy through this function when attaching a cutscene prop to a bone.
    {0x406620,0x57F250,"cutscene attach calls the skin clump hierarchy getter"},
};
constexpr ByteCheck byteChecks10EN[]={
    {0x40650D,7,{0xC6,0x05,0xB2,0x0A,0xA1,0,1},"cutscene running flag"},
    {0x517214,6,{0x8B,0x8B,0xF8,0x01,0,0},"ped vehicle animation field"},
    {0x4063B8,6,{0x8B,0x1D,0x74,0xFC,0xA0,0},"cutscene object count"},
    {0x406402,7,{0x8B,0x0C,0x9D,0x88,0x82,0x93,0},"cutscene object array"},
    {0x406640,6,{0x89,0xAB,0x9C,0x01,0,0},"cutscene attachment field"},
    {0x4FE25D,8,{0xD9,0x06,0x8D,0x50,0x10,0x50,0xD9,0x1A},"weapon matrix copy in CPed::Render"},
    {0x57F250,11,{0x83,0xEC,0x08,0xC7,0x44,0x24,0x04,0,0,0,0},"skin clump hierarchy getter"},
    {0x646370,8,{0x8B,0x44,0x24,0x04,0x8B,0x40,0x08,0xC3},"HAnim matrix array getter"},
    {0x64CA4D,3,{0x8B,0x46,0x14},"geometry vertex count offset"},
    {0x64CA69,3,{0x8B,0x46,0x5C},"geometry morph target offset"},
    {0x589790,9,{0x53,0x56,0x89,0xCB,0x57,0x55,0x83,0xEC,0x18},"CAutomobile::Render prologue"},
    {0x4A61D6,5,{0xE8,0x05,0xAA,0x17,0},"drawing event call"},
    // CGame::Process tests CReplay::Mode == 1 here; this is the game's own replay-playback check.
    {0x4A4612,7,{0x80,0x3D,0xF6,0x0A,0xA1,0,1},"replay mode flag"},
    {0x644D00,15,{0x8B,0x44,0x24,0x04,0x33,0xC9,0x8B,0x90,0xA0,0,0,0,0x8A,0x4A,0x03},"RwFrameUpdateObjects"},
    {0x644D80,15,{0x56,0x8B,0x74,0x24,0x08,0x8B,0x86,0xA0,0,0,0,0xF6,0x40,0x03,0x01},"RwFrameGetLTM"},
    // Model info pointer table, as read by CStreaming (Plugin-SDK uses the same reference).
    {0x40B821,7,{0x8B,0x04,0x85,0xC8,0xD4,0x92,0},"model info pointer table"},
};
constexpr GameLayout layout10EN={
    "1.0 EN",
    0x92D4C8,0xA10AF6,0xA10AB2,0xA0FC74,0x938288,
    {0x69AD90,0x589AD0,0x589790},    // CAutomobile
    {0x69B0B4,0x5A5570,0x5A4DE0},    // CBoat
    {0x69C21C,0x5AF690,0x5AF1A0},    // CHeli
    {0x69C3F8,0x5B2590,0x5B2570},    // CPlane
    {0x6D7B34,0x60AD90,0x60AD50},    // CBike
    0x69158C,0x4E0540,0x4896C0,0x4E0130,
    0x644D00,0x644D80,0x57F250,
    0x5427A0,0x5423E0,0x567700,0x571820,0x56DCD0,
    Whole(callChecks10EN),Whole(byteChecks10EN),Whole(lightCallsites10EN),
};

} // namespace vicebounce
