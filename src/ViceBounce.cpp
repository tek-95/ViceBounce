#include "plugin.h"
#include "CVehicle.h"
#include "CAutomobile.h"
#include "CBike.h"
#include "CVehicleModelInfo.h"
#include "CColModel.h"
#include "CPed.h"
#include "CCutsceneObject.h"
#include "CAnimBlendAssociation.h"
#include "CTimer.h"
#include "eVehicleType.h"
#include "BounceMath.h"
#include "Vegetation.h"
#include "Buildings.h"
#include "CSimpleModelInfo.h"
#include "SceneryGroups.h"
#include "CPools.h"
#include "CColPoint.h"
#include <unordered_map>
#include <windows.h>
#include <array>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdio>
#include <vector>
#include <type_traits>
#include "LightCallsites.h"
#include "GameLayoutCheck.h"
#include "SceneryLayout.h"
#include "Diagnostics.h"
#include "MusicPlayback.h"
#include "BeatMap.h"
#include "MusicImporter.h"
#include <filesystem>

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace {
using namespace vicebounce;
const GameLayout* layout=nullptr;
bool retailExecutable=false;
uintptr_t GameAddress(uintptr_t address){return plugin::GetGlobalAddress(AddressForVersion(address,retailExecutable));}
std::vector<LightCallsite> lightCallsites;
Settings cfg;
// Every verified Steam vtable inheriting CEntity::PreRender, including map
// buildings, their LODs, dummies and movable/interior objects.
uintptr_t vegetationVtables[]={0x68558C,0x6855DC,0x68A360,0x68A5DC,0x68E6BC,0x6905E4,0x6907A8,0x69B9F4};
bool vegetationModels[6500]{};
bool vegetationModelsReady=false;
ULONGLONG nextSceneryRefresh=0;
double phase=0;
std::string iniPath,logPath;
HANDLE exceptionLog=INVALID_HANDLE_VALUE;
volatile LONG recordedExceptions=0;
thread_local const char* diagnosticStage="outside scenery";
thread_local int diagnosticModel=-1;
struct DiagnosticScope {
    const char* previousStage=diagnosticStage;int previousModel=diagnosticModel;
    DiagnosticScope(const char* stage,int model=-1){diagnosticStage=stage;diagnosticModel=model;DIAG_STAGE(stage);}
    ~DiagnosticScope(){diagnosticStage=previousStage;diagnosticModel=previousModel;DIAG_STAGE(previousStage);}
};
LONG CALLBACK RecordException(EXCEPTION_POINTERS* info) {
    const auto code=info->ExceptionRecord->ExceptionCode;
    if((code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_ILLEGAL_INSTRUCTION&&code!=EXCEPTION_INT_DIVIDE_BY_ZERO)||exceptionLog==INVALID_HANDLE_VALUE)
        return EXCEPTION_CONTINUE_SEARCH;
    if(InterlockedIncrement(&recordedExceptions)>8) return EXCEPTION_CONTINUE_SEARCH;
    const auto& c=*info->ContextRecord;
    char line[768];
    const int n=_snprintf_s(line,sizeof(line),_TRUNCATE,
        "exception tick=%llu code=%08lX pc=%08lX mod_base=%p game_base=%p stage=%s model=%d eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX esp=%08lX access=%llu address=%p\r\n",
        GetTickCount64(),code,c.Eip,&__ImageBase,GetModuleHandleA(nullptr),diagnosticStage,diagnosticModel,
        c.Eax,c.Ebx,c.Ecx,c.Edx,c.Esi,c.Edi,c.Esp,
        code==EXCEPTION_ACCESS_VIOLATION?static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[0]):0,
        code==EXCEPTION_ACCESS_VIOLATION?reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]):nullptr);
    if(n>0){DWORD written;WriteFile(exceptionLog,line,n,&written,nullptr);FlushFileBuffers(exceptionLog);}
    return EXCEPTION_CONTINUE_SEARCH; // Record evidence; never swallow a fault.
}
MusicPlayback music;
std::wstring musicFile;
std::wstring musicDirectory,beatMapFile;
BeatMap beatMap;
MusicImporter importer;
bool importRequested=false,useBeatMap=true;
double musicSyncOffsetMs=0;
bool musicEnabled=true,musicPending=false,musicErrorLogged=false,musicStartedLogged=false;
double firstBeatMs=0;
using UpdateFrameFn=RwFrame*(__cdecl*)(RwFrame*);
using GetLtmFn=RwMatrix*(__cdecl*)(RwFrame*);
UpdateFrameFn updateFrame=nullptr;
GetLtmFn getLtm=nullptr;
using RenderFn=void(__thiscall*)(CVehicle*);
std::array<RenderFn,6> originalRenders{},originalPreRenders{};
struct TransportHook { int runtimeType;uintptr_t vtable,preRender,render; };
TransportHook transportHooks[]={
    {VEHICLE_AUTOMOBILE,0x699D90,0x589900,0x5895C0},
    {VEHICLE_BOAT,0x69A0B4,0x5A53A0,0x5A4C10},
    {VEHICLE_HELI,0x69B21C,0x5AF4C0,0x5AEFD0},
    {VEHICLE_PLANE,0x69B3F8,0x5B23C0,0x5B23A0},
    {VEHICLE_BIKE,0x6D6B04,0x60A9B0,0x60A970}
};
using CutsceneRenderFn=void(__thiscall*)(CCutsceneObject*);
CutsceneRenderFn originalCutsceneRender=nullptr;
using CutsceneDeleteFn=void*(__thiscall*)(CCutsceneObject*,unsigned);
CutsceneDeleteFn originalCutsceneDelete=nullptr;
using CutsceneDeleteRwFn=void(__thiscall*)(CCutsceneObject*);
CutsceneDeleteRwFn originalCutsceneDeleteRw=nullptr;
uintptr_t cutsceneVtable=0x690594,cutsceneRender=0x4DFFF0;
CVehicle* lightingVehicle=nullptr;
struct SceneryLighting {RwMatrix basis;Scale scale;};
const SceneryLighting* lightingScenery=nullptr;
CVehicleModelInfo* ModelInfo(CVehicle* vehicle) {
    if(!vehicle||vehicle->m_nModelIndex<0||vehicle->m_nModelIndex>=6500) return nullptr;
    auto** models=reinterpret_cast<CVehicleModelInfo**>(GameAddress(0x92C4D0));
    auto* model=models[vehicle->m_nModelIndex];
    // Airtrain LOD is a SIMPLE model: never read the vehicle-only tail there.
    return model&&model->m_nType==MODEL_INFO_VEHICLE?model:nullptr;
}
int TransportKind(CVehicle* vehicle) {
    if(auto* model=ModelInfo(vehicle)) return model->m_nVehicleType;
    return vehicle->m_nVehicleClass;
}

bool ReplayPlaying() {
    // Steam CReplay::Mode; the old SDK reference points to the 1.0 global.
    return *reinterpret_cast<const unsigned char*>(GameAddress(0xA0FAF6))==1;
}
bool CutscenePlaying() {
    // Steam StartCutscene writes this byte at 0x40650D.
    return *reinterpret_cast<const unsigned char*>(GameAddress(0xA0FABA))!=0;
}
bool Paused() { return VisualPaused(CTimer::m_UserPause,CTimer::m_CodePause,ReplayPlaying(),CutscenePlaying()); }
bool EffectActive(CVehicle* vehicle) { return vehicle&&VehicleEnabled(cfg,TransportKind(vehicle))&&cfg.amplitude>0&&!Paused(); }
Scale ActiveScale() { auto visual=cfg;visual.enabled=true;return GetScale(phase,visual); }
void RestoreFrames();

void Log(const std::string& text) {
    std::ofstream out(logPath,std::ios::app);
    out << GetTickCount64() << " " << text << '\n';
}
void Status(const char* action) {
    char value[160];
    std::snprintf(value,sizeof(value),"%s cars=%d other=%d people=%d plants=%d bpm=%.0f amplitude=%.3f easing=%d",action,cfg.enabled,cfg.otherVehicles,cfg.allPeople,cfg.vegetation,cfg.bpm,cfg.amplitude,cfg.easing);
    Log(value);
}
void LoadConfig(bool requestImport=true) {
    char value[96];
    GetPrivateProfileStringA("ViceBounce","BPM","135",value,sizeof(value),iniPath.c_str());
    cfg.bpm=ParseNumber(value,135,20,300);
    GetPrivateProfileStringA("ViceBounce","Amplitude","0.20",value,sizeof(value),iniPath.c_str());
    cfg.amplitude=ParseNumber(value,0.20,0,0.75);
    GetPrivateProfileStringA("ViceBounce","Easing","1",value,sizeof(value),iniPath.c_str());
    cfg.easing=int(ParseNumber(value,1,0,3));
    GetPrivateProfileStringA("ViceBounce","Enabled","1",value,sizeof(value),iniPath.c_str());
    cfg.enabled=ParseNumber(value,1,0,1)>=0.5;
    GetPrivateProfileStringA("ViceBounce","OtherVehicles","1",value,sizeof(value),iniPath.c_str());
    cfg.otherVehicles=ParseNumber(value,1,0,1)>=0.5;
    GetPrivateProfileStringA("ViceBounce","AllPeople","0",value,sizeof(value),iniPath.c_str());
    cfg.allPeople=ParseNumber(value,0,0,1)>=0.5;
    GetPrivateProfileStringA("ViceBounce","Vegetation","0",value,sizeof(value),iniPath.c_str());
    cfg.vegetation=ParseNumber(value,0,0,1)>=0.5;
    GetPrivateProfileStringA("ViceBounce","VegetationStrength","0.275",value,sizeof(value),iniPath.c_str());
    cfg.vegetationStrength=ParseNumber(value,.275,0,1);
    GetPrivateProfileStringA("ViceBounce","VegetationMaxDisplacement","1.0",value,sizeof(value),iniPath.c_str());
    cfg.vegetationMaxDisplacement=ParseNumber(value,1,0,2);
    GetPrivateProfileStringA("ViceBounce","Buildings","0",value,sizeof(value),iniPath.c_str());
    cfg.buildings=ParseNumber(value,0,0,1)>=.5;
    GetPrivateProfileStringA("ViceBounce","BuildingStrength","0.30",value,sizeof(value),iniPath.c_str());
    cfg.buildingStrength=ParseNumber(value,.30,0,1);
    GetPrivateProfileStringA("ViceBounce","BuildingMaxDisplacement","4.0",value,sizeof(value),iniPath.c_str());
    cfg.buildingMaxDisplacement=ParseNumber(value,4,0,10);
    GetPrivateProfileStringA("ViceBounce","StreetObjects","0",value,sizeof(value),iniPath.c_str());
    cfg.streetObjects=ParseNumber(value,0,0,1)>=.5;
    GetPrivateProfileStringA("ViceBounce","StreetStrength","1.0",value,sizeof(value),iniPath.c_str());
    cfg.streetStrength=ParseNumber(value,1,0,1);
    nextSceneryRefresh=0;
    vegetationModelsReady=false;
    GetPrivateProfileStringA("ViceBounce","MusicEnabled","1",value,sizeof(value),iniPath.c_str());
    musicEnabled=ParseNumber(value,1,0,1)>=0.5;
    GetPrivateProfileStringA("ViceBounce","MusicFirstBeatMs","0",value,sizeof(value),iniPath.c_str());
    firstBeatMs=ParseNumber(value,0,-3600000,3600000);
    wchar_t module[MAX_PATH]{},path[1024]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),module,MAX_PATH);
    std::wstring folder(module);folder=folder.substr(0,folder.find_last_of(L"\\/")+1);
    musicDirectory=folder;
    const auto wideIni=folder+L"ViceBounce.ini";
    GetPrivateProfileStringW(L"ViceBounce",L"MusicFile",L"ViceBounceMusic.mp3",path,1024,wideIni.c_str());
    musicFile=path;
    if(!musicFile.empty()&&musicFile.find(L':')==std::wstring::npos&&musicFile[0]!=L'\\'&&musicFile[0]!=L'/') musicFile=folder+musicFile;
    GetPrivateProfileStringW(L"ViceBounce",L"MusicBeatMapFile",L"",path,1024,wideIni.c_str());
    beatMapFile=path;
    if(!beatMapFile.empty()&&beatMapFile.find(L':')==std::wstring::npos&&beatMapFile[0]!=L'\\'&&beatMapFile[0]!=L'/') beatMapFile=folder+beatMapFile;
    GetPrivateProfileStringA("ViceBounce","MusicUseBeatMap","1",value,sizeof(value),iniPath.c_str());
    useBeatMap=ParseNumber(value,1,0,1)>=.5;
    GetPrivateProfileStringA("ViceBounce","MusicSyncOffsetMs","0",value,sizeof(value),iniPath.c_str());
    musicSyncOffsetMs=ParseNumber(value,0,-3600000,3600000);
    GetPrivateProfileStringW(L"ViceBounce",L"MusicSourceFile",L"",path,1024,wideIni.c_str());
    std::wstring source(path);
    if(!source.empty()&&source.find(L':')==std::wstring::npos&&source[0]!=L'\\'&&source[0]!=L'/') source=folder+source;
    GetPrivateProfileStringA("ViceBounce","MusicAutoImport","1",value,sizeof(value),iniPath.c_str());
    const bool autoImport=ParseNumber(value,1,0,1)>=.5;
    importer.Configure(source,autoImport);
    importRequested=requestImport&&musicEnabled&&autoImport&&!source.empty();
    musicPending=true;musicErrorLogged=false;musicStartedLogged=false;
    Status("config loaded");
}
void SaveValue(const char* name,double value) {
    char text[40];std::snprintf(text,sizeof(text),"%.4f",value);
    WritePrivateProfileStringA("ViceBounce",name,text,iniPath.c_str());
}
bool Focused() {
    DWORD process=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&process);
    return process==GetCurrentProcessId();
}
void MusicPickerControls() {
    const int result=importer.Poll();
    if(result==0) {LoadConfig(false);Log("local music import/check completed; reloading track profile");}
    else if(result>0&&result!=2) Log("music import failed; see ViceBounceImportStatus.json; previous profile retained");
    static bool wasDown=false;
    const bool down=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
    const bool pressed=down&&!wasDown;wasDown=down;
    // Music selection is also available on pause and before loading a save.
    // Ctrl+Shift+F11 remains available for OBS.
    if(pressed&&Focused()&&!(GetAsyncKeyState(VK_CONTROL)&0x8000)&&!(GetAsyncKeyState(VK_SHIFT)&0x8000)&&!(GetAsyncKeyState(VK_MENU)&0x8000)) {
        if(importer.Start(musicDirectory,true)) Log("music file picker opened");
        else Log("music picker unavailable or import already running");
    }
}
// Plugin-SDK's VC menu event currently only defines 1.0 addresses. These two
// Steam callsites both call CMenuManager::Draw (0x4A3663), verified below.
plugin::ThiscallEvent<plugin::AddressListMulti<
    0x4A5C03, GAME_STEAM, plugin::H_CALL,
    0x4A5FFE, GAME_STEAM, plugin::H_CALL,
    0x4A5D33, GAME_10EN, plugin::H_CALL,
    0x4A612E, GAME_10EN, plugin::H_CALL>,
    plugin::PRIORITY_BEFORE, plugin::ArgPickNone, void(CMenuManager*)> steamMenuDrawingEvent;
void Tick() {
#ifdef VICEBOUNCE_DIAGNOSTICS
    static bool firstDiagnosticFrame=true;
    if(firstDiagnosticFrame){firstDiagnosticFrame=false;diag::ListModules();diag::StartWatchdog();}
    ++diag::frames;
#endif
    // Runs at the 3D/2D boundary, after deferred transparent vehicle atomics.
    RestoreFrames();
    static ULONGLONG previous=GetTickCount64();
    static std::array<bool,13> wasDown{};
    constexpr int keys[]={VK_F6,VK_F7,VK_PRIOR,VK_NEXT,VK_HOME,VK_END,VK_F8,VK_F9,VK_F10,VK_F11,VK_F5,VK_F4,VK_F3};
    const auto now=GetTickCount64();
    const double elapsed=(now-previous)/1000.0;previous=now;
    const bool active=Focused()&&!Paused();
    // Open audio lazily on the game thread, never from ASI/DllMain initialization.
    MusicPickerControls();
    if(importRequested) {
        importRequested=false;
        if(importer.Start(musicDirectory)) {music.Configure(false,L"");Log("checking selected music file in background");}
        else Log("automatic music import unavailable; existing track profile retained");
    }
    if(musicEnabled&&importer.Changed()&&importer.Start(musicDirectory)) {
        music.Configure(false,L"");musicPending=true;Log("source music changed; rebuilding local track profile");
    }
    if(musicPending&&!importer.Running()) {
        music.Configure(musicEnabled,musicFile);musicPending=false;beatMap=BeatMap{};
        if(useBeatMap&&!beatMapFile.empty()) {
            std::ifstream input{std::filesystem::path(beatMapFile)};
            if(beatMap.Load(input)) Log("loaded beat map: "+std::to_string(beatMap.Size())+" beats");
            else Log("beat map invalid/missing; configured BPM and first beat used instead");
        }
    }
    double musicPosition=0;
    const bool synced=music.Position(musicPosition);
    if(synced) {
        phase=useBeatMap&&beatMap.Valid()?beatMap.Phase(musicPosition-musicSyncOffsetMs):MusicPhase(musicPosition,firstBeatMs,cfg.bpm);
        if(!musicStartedLogged){Log("music playback active; track position controls squash on beats");musicStartedLogged=true;}
    } else if(musicEnabled&&music.Error()&&!musicErrorLogged) {
        Log("music unavailable MCI error="+std::to_string(music.Error())+"; visual fallback active; set MusicFile and press F7");musicErrorLogged=true;
    }
    std::array<bool,13> pressed{};
    for(size_t i=0;i<wasDown.size();++i) {
        const bool down=(GetAsyncKeyState(keys[i])&0x8000)!=0;
        pressed[i]=active&&down&&!wasDown[i];wasDown[i]=down;
    }
    if(!active) return;
    if(!synced&&(cfg.enabled||cfg.otherVehicles||cfg.allPeople||cfg.vegetation||cfg.buildings||cfg.streetObjects)&&elapsed<=0.25) phase=AdvancePhase(phase,elapsed,cfg.bpm);
    if(pressed[12]){cfg.streetObjects=!cfg.streetObjects;SaveValue("StreetObjects",cfg.streetObjects);Log(cfg.streetObjects?"F3 street objects on":"F3 street objects off");}
    if(pressed[11]){cfg.buildings=!cfg.buildings;SaveValue("Buildings",cfg.buildings);Log(cfg.buildings?"F4 buildings on":"F4 buildings off");}
    if(pressed[10]){cfg.vegetation=!cfg.vegetation;SaveValue("Vegetation",cfg.vegetation);Status("F5 vegetation toggle");}
    if(pressed[0]){cfg.enabled=!cfg.enabled;SaveValue("Enabled",cfg.enabled);Status("F6 toggle");}
    if(pressed[1]) LoadConfig();
    if(pressed[2]||pressed[3]){
        cfg.bpm=std::clamp(cfg.bpm+(pressed[2]?5:-5),20.0,300.0);
        useBeatMap=false;SaveValue("MusicUseBeatMap",0);SaveValue("BPM",cfg.bpm);Status("manual tempo changed; beat map disabled");
    }
    if(pressed[4]||pressed[5]){cfg.amplitude=std::clamp(cfg.amplitude+(pressed[4]?0.025:-0.025),0.0,0.75);SaveValue("Amplitude",cfg.amplitude);Status("amplitude changed");}
    if(pressed[6]){cfg.otherVehicles=!cfg.otherVehicles;SaveValue("OtherVehicles",cfg.otherVehicles);Status("F8 other transport toggle");}
    if(pressed[7]){
        phase=.5;
        if(synced){
            if(useBeatMap&&beatMap.Valid()) {
                musicSyncOffsetMs=musicPosition-beatMap.Nearest(musicPosition-musicSyncOffsetMs);
                SaveValue("MusicSyncOffsetMs",musicSyncOffsetMs);
            } else {firstBeatMs=musicPosition;SaveValue("MusicFirstBeatMs",firstBeatMs);}
        }
        Status(synced?"F9 current track position marked as squash beat":"F9 squash beat phase reset");
    }
    if(pressed[8]){cfg.allPeople=!cfg.allPeople;SaveValue("AllPeople",cfg.allPeople);Status("F10 all people toggle");}
}

struct SavedFrame { RwFrame* frame=nullptr; RwMatrix original{}; bool scenery=false; };
std::vector<SavedFrame> savedFrames;
struct SavedBones { RwMatrix* matrices; std::vector<RwMatrix> originals; };
std::vector<SavedBones> savedBones;
struct VehicleGroundOffset { CVehicle* vehicle; float z; };
std::vector<VehicleGroundOffset> groundOffsets;

void WheelGroundPoints(RwFrame* frame,const CMatrix& basis,Scale scale,GroundAnchor& anchor,int depth=0) {
    if(!frame||depth>=16) return;
    auto* sentinel=&frame->objectList.link;
    int objects=0;
    const auto* world=getLtm(frame);
    for(auto* link=sentinel->next;link!=sentinel&&objects++<64;link=link->next) {
        auto* object=rwLLLinkGetData(link,RwObjectHasFrame,lFrame);
        if(object->object.type!=rpATOMIC||!(object->object.flags&rpATOMICRENDER)) continue;
        auto* atomic=reinterpret_cast<RpAtomic*>(object);
        if(!atomic->geometry) continue;
        // Steam RW geometry omits the SDK header's obsolete surface-properties field.
        const auto* geometry=reinterpret_cast<const unsigned char*>(atomic->geometry);
        const int count=*reinterpret_cast<const int*>(geometry+0x14);
        const int targets=*reinterpret_cast<const int*>(geometry+0x18);
        const auto* morph=*reinterpret_cast<RpMorphTarget*const*>(geometry+0x5C);
        if(count<=0||count>32768||targets<=0||!morph||!morph->verts) continue;
        for(int i=0;i<count;++i) {
            const auto& local=morph->verts[i];
            RwV3d position{
                world->pos.x+world->right.x*local.x+world->up.x*local.y+world->at.x*local.z,
                world->pos.y+world->right.y*local.x+world->up.y*local.y+world->at.y*local.z,
                world->pos.z+world->right.z*local.x+world->up.z*local.y+world->at.z*local.z};
            anchor.Add(position.z,DeformPosition(position,basis,scale).z);
        }
    }
    for(auto* child=frame->child;child;child=child->next) WheelGroundPoints(child,basis,scale,anchor,depth+1);
}
void VegetationBottom(RwFrame* frame,VegetationAnchor& anchor,int depth=0) {
    if(!frame||depth>=16) return;
    const auto* world=getLtm(frame);
    auto* sentinel=&frame->objectList.link;
    int objects=0;
    for(auto* link=sentinel->next;link!=sentinel&&objects++<64;link=link->next) {
        auto* object=rwLLLinkGetData(link,RwObjectHasFrame,lFrame);
        // Root atomics are rendered directly by CEntity, including alpha/LOD
        // paths that don't use the clump child render-flag filter.
        if(object->object.type!=rpATOMIC||(depth>0&&!(object->object.flags&rpATOMICRENDER))) continue;
        auto* atomic=reinterpret_cast<RpAtomic*>(object);
        if(!atomic->geometry) continue;
        // Same verified Steam geometry layout used by the wheel ground anchor.
        const auto* geometry=reinterpret_cast<const unsigned char*>(atomic->geometry);
        const int count=*reinterpret_cast<const int*>(geometry+0x14);
        const int targets=*reinterpret_cast<const int*>(geometry+0x18);
        const auto* morph=*reinterpret_cast<RpMorphTarget*const*>(geometry+0x5C);
        if(count<=0||count>131072||targets<=0||!morph||!morph->verts) continue;
        for(int i=0;i<count;++i) {
            const auto& p=morph->verts[i];
            const RwV3d point{
                world->pos.x+world->right.x*p.x+world->up.x*p.y+world->at.x*p.z,
                world->pos.y+world->right.y*p.x+world->up.y*p.y+world->at.y*p.z,
                world->pos.z+world->right.z*p.x+world->up.z*p.y+world->at.z*p.z};
            anchor.Add(point);
        }
    }
    for(auto* child=frame->child;child;child=child->next) VegetationBottom(child,anchor,depth+1);
}
bool VegetationModel(CEntity* entity) {
    const int modelIndex=entity->m_nModelIndex;
    if(modelIndex<0||modelIndex>=6500) return false;
    if(!vegetationModelsReady) {
        auto** models=reinterpret_cast<CBaseModelInfo**>(GameAddress(0x92C4D0));
        unsigned count=0;
        for(int i=0;i<6500;++i) {
            const auto* model=models[i];
            vegetationModels[i]=model&&VegetationModelName(model->m_szName,sizeof(model->m_szName));
            if(vegetationModels[i]) ++count;
        }
        vegetationModelsReady=true;Log("vegetation model catalog ready: "+std::to_string(count)+" models; F7 refreshes catalog");
    }
    return vegetationModels[modelIndex];
}
struct CachedMeshBounds { const RwV3d* vertices=nullptr;int count=0;RwV3d first{},last{};SceneryBounds bounds; };
std::unordered_map<const void*,CachedMeshBounds> meshBounds;
void SceneryMeshBounds(RwFrame* frame,SceneryBounds& bounds,int depth=0) {
    if(!frame||depth>=16) return;
    const auto* world=getLtm(frame);
    auto* sentinel=&frame->objectList.link;int objects=0;
    for(auto* link=sentinel->next;link!=sentinel&&objects++<64;link=link->next) {
        auto* object=rwLLLinkGetData(link,RwObjectHasFrame,lFrame);
        if(object->object.type!=rpATOMIC||(depth>0&&!(object->object.flags&rpATOMICRENDER))) continue;
        auto* atomic=reinterpret_cast<RpAtomic*>(object);
        if(!atomic->geometry) continue;
        const auto* geometry=reinterpret_cast<const unsigned char*>(atomic->geometry);
        const int count=*reinterpret_cast<const int*>(geometry+0x14),targets=*reinterpret_cast<const int*>(geometry+0x18);
        const auto* morph=*reinterpret_cast<RpMorphTarget*const*>(geometry+0x5C);
        if(count<=0||count>131072||targets<=0||!morph||!morph->verts) continue;
        auto& cache=meshBounds[geometry];const auto* vertices=morph->verts;
        if(cache.vertices!=vertices||cache.count!=count||std::memcmp(&cache.first,vertices,sizeof(RwV3d))||std::memcmp(&cache.last,vertices+count-1,sizeof(RwV3d))) {
            cache=CachedMeshBounds{};cache.vertices=vertices;cache.count=count;cache.first=vertices[0];cache.last=vertices[count-1];
            for(int i=0;i<count;++i) cache.bounds.Add(vertices[i]);
        }
        if(!cache.bounds.Valid()) continue;
        const auto& b=cache.bounds;
        for(float x:{b.x0,b.x1}) for(float y:{b.y0,b.y1}) for(float z:{b.z0,b.z1})
            bounds.Add(RwV3d{world->pos.x+world->right.x*x+world->up.x*y+world->at.x*z,
                world->pos.y+world->right.y*x+world->up.y*y+world->at.y*z,
                world->pos.z+world->right.z*x+world->up.z*y+world->at.z*z});
    }
    for(auto* child=frame->child;child;child=child->next) SceneryMeshBounds(child,bounds,depth+1);
}
CBaseModelInfo* SceneryModel(CEntity* entity) {
    if(!entity||entity->m_nModelIndex<0||entity->m_nModelIndex>=6500) return nullptr;
    return reinterpret_cast<CBaseModelInfo**>(GameAddress(0x92C4D0))[entity->m_nModelIndex];
}
bool ModelIsRoad(CBaseModelInfo* model) {
    return (model->m_nType==MODEL_INFO_SIMPLE||model->m_nType==MODEL_INFO_TIME)&&static_cast<CSimpleModelInfo*>(model)->m_nFlags.bIsRoad;
}
struct GroundPlaneCache {SceneryBounds bounds;float z;bool valid;ULONGLONG expires;};
std::vector<GroundPlaneCache> sceneryGroundPlanes;
bool sceneryGroundQueriesAllowed=false;
std::vector<SceneryBounds> pendingGroundQueries;
bool SceneryGroundPlane(const SceneryBounds& bounds,float& ground) {
    const auto now=GetTickCount64();
    for(auto& saved:sceneryGroundPlanes) if(std::memcmp(&saved.bounds,&bounds,sizeof(bounds))==0&&now<saved.expires) {ground=saved.z;return saved.valid;}
    if(!sceneryGroundQueriesAllowed) {
        // Render hooks only consume cached support. World collision queries
        // touch scan codes and collision caches; defer them to game processing.
        bool queued=false;
        for(const auto& pending:pendingGroundQueries) if(std::memcmp(&pending,&bounds,sizeof(bounds))==0){queued=true;break;}
        if(!queued&&pendingGroundQueries.size()<2048) pendingGroundQueries.push_back(bounds);
        ground=bounds.z0;return false;
    }
    DiagnosticScope diagnostic("scenery ground trace");
    using TraceFn=bool(__cdecl*)(const CVector&,float,CColPoint&,CEntity*&,bool,bool,bool,bool,bool,bool,void*);
    const auto trace=reinterpret_cast<TraceFn>(GameAddress(0x4D89C0));
    std::vector<float> samples;
    size_t probeIndex=0;
    for(const auto& sample:SceneryGroundProbes(bounds)) {
        if(probeIndex++==13&&!samples.empty()) break;
        CVector origin(sample.x,sample.y,bounds.z1+3);
        for(int layer=0;layer<12;++layer) {
            CColPoint point{};CEntity* hit=nullptr;
            if(!trace(origin,-1000,point,hit,true,false,false,false,false,true,nullptr)||!hit||!std::isfinite(point.m_vecPoint.z)) break;
            auto* model=SceneryModel(hit);
            const float z=point.m_vecPoint.z;
            // Reject roofs/awnings: accept only road or identified land/lawn
            // collision meshes. Vehicles and pedestrians are excluded above.
            const bool contactFloor=model&&!StreetObjectName(SceneryName(model->m_szName))&&std::abs(z-bounds.z0)<=1.f&&point.m_vNormal.z>.8f;
            if(model&&(ModelIsRoad(model)||TerrainModelName(model->m_szName)||contactFloor)&&point.m_vNormal.z>.25f) {samples.push_back(z);break;}
            if(z>=origin.z-.001f) origin.z-=.1f;else origin.z=z-.1f;
        }
    }
    const bool valid=!samples.empty();ground=bounds.z0;
    if(valid) {std::sort(samples.begin(),samples.end());ground=samples[samples.size()/2];}
    if(sceneryGroundPlanes.size()>3000) sceneryGroundPlanes.clear();
    sceneryGroundPlanes.push_back({bounds,ground,valid,now+(valid?10000:1000)});
    return valid;
}
struct GroupedBuilding { short model;RwObject* object;CVector position;SceneryBounds bounds;bool groundFound; };
std::unordered_map<CEntity*,GroupedBuilding> groupedBuildings;
void RefreshSceneryGroups() {
    // Called only after restoring prior render matrices, before pre-render.
    if(!cfg.buildings||GetTickCount64()<nextSceneryRefresh) return;
    nextSceneryRefresh=GetTickCount64()+500;
    if(meshBounds.size()>10000) meshBounds.clear();
    std::vector<CEntity*> entities;std::vector<SceneryBounds> bounds;std::vector<bool> core,expand;
    auto collect=[&](auto* pool) {
        if(!pool||!pool->m_pObjects||!pool->m_byteMap||pool->m_nSize<0||pool->m_nSize>20000) return;
        for(int i=0;i<pool->m_nSize;++i) {
            auto* entity=pool->GetAt(i);
            if(!entity||!entity->m_pRwObject||!RenderableObjectType(entity->m_pRwObject->type)) continue;
            auto* model=SceneryModel(entity);
            if(!model||!BuildingCandidate(entity->m_nType,VegetationModelName(model->m_szName),ModelIsRoad(model),model->m_szName)) continue;
            auto* frame=static_cast<RwFrame*>(entity->m_pRwObject->parent);if(!frame) continue;
            SceneryBounds b;SceneryMeshBounds(frame,b);if(!b.Valid()) continue;
            const auto name=SceneryName(model->m_szName);
            const bool body=name.rfind("lod",0)!=0&&!BuildingDetailName(name)&&b.z1-b.z0>=2&&(b.x1-b.x0)*(b.y1-b.y0)>=4;
            entities.push_back(entity);bounds.push_back(b);core.push_back(body);expand.push_back(name.rfind("lod",0)!=0);
        }
    };
    collect(CPools::ms_pBuildingPool);collect(CPools::ms_pTreadablePool);
    const auto groups=GroupScenery(bounds,core,expand);groupedBuildings.clear();
    // If the merged footprint misses land, use body footprints and share
    // the resulting support with every facade, attachment and LOD.
    const auto supports=SharedSceneryGround(bounds,groups,core,SceneryGroundPlane);
    for(size_t i=0;i<entities.size();++i) {
        auto* e=entities[i];const auto& p=e->GetMatrix().pos;
        auto grounded=groups[i];
        const float ground=supports[i].z;
        const bool found=supports[i].found;
        if(found) grounded.z0=ground;
        groupedBuildings.emplace(e,GroupedBuilding{e->m_nModelIndex,e->m_pRwObject,CVector(p.x,p.y,p.z),grounded,found});
    }
    static bool logged=false;if(!logged&&!entities.empty()){Log("shared building transforms ready for "+std::to_string(entities.size())+" loaded parts; actual mesh bounds replace collision placeholders");logged=true;}
}
void __fastcall VegetationPreRender(CEntity* plant,void*) {
    DiagnosticScope diagnostic("scenery PreRender",plant?plant->m_nModelIndex:-1);
    // A tree may be prepared twice through visible/alpha lists. Native wind
    // rewrites its frame each time; start again from the saved native matrix
    // and reapply once, rather than skipping after wind erased the bounce.
    if(plant&&plant->m_pRwObject) {
        auto* root=static_cast<RwFrame*>(plant->m_pRwObject->parent);
        for(auto i=savedFrames.begin();i!=savedFrames.end();++i) if(i->scenery&&i->frame==root) {
            *RwFrameGetMatrix(root)=i->original;updateFrame(root);getLtm(root);savedFrames.erase(i);
            static bool logged=false;if(!logged){Log("repeated scenery PreRender safely restored and reapplied; native wind cannot erase bounce");logged=true;}
            break;
        }
    }
    // Run normal pre-render first: preserves wind, rotor downwash, lights and
    // pickup handling. This also runs for plants with no native wind effect.
    struct NativePreRender {
        CEntity* entity;bool called=false;
        void Invoke(const SceneryLighting* light=nullptr) {
            if(called) return;called=true;
            const auto* previous=lightingScenery;lightingScenery=light;
            reinterpret_cast<void(__thiscall*)(CEntity*)>(GameAddress(0x4886F0))(entity);
            lightingScenery=previous;
        }
        ~NativePreRender(){Invoke();}
    } native{plant};
    if((!cfg.vegetation&&!cfg.buildings&&!cfg.streetObjects)||cfg.amplitude<=0||Paused()||!plant->m_pRwObject||!RenderableObjectType(plant->m_pRwObject->type)) return;
    const bool vegetation=VegetationModel(plant);
    if(vegetation) native.Invoke(); // Native wind must precede plant deformation.
    auto* model=SceneryModel(plant);if(!model||TerrainModelName(model->m_szName)) return;
    const bool street=!vegetation&&StreetObjectName(SceneryName(model->m_szName))&&!ModelIsRoad(model);
    CBaseModelInfo* buildingModel=nullptr;
    if(vegetation) {if(!cfg.vegetation||cfg.vegetationStrength<=0) return;}
    else if(street) {if(!cfg.streetObjects||cfg.streetStrength<=0) return;}
    else {
        if(!cfg.buildings||cfg.buildingStrength<=0||plant->m_nModelIndex<0||plant->m_nModelIndex>=6500) return;
        auto** models=reinterpret_cast<CBaseModelInfo**>(GameAddress(0x92C4D0));
        buildingModel=models[plant->m_nModelIndex];
        if(!buildingModel) return;
        const bool simple=buildingModel->m_nType==MODEL_INFO_SIMPLE||buildingModel->m_nType==MODEL_INFO_TIME;
        const bool road=simple&&static_cast<CSimpleModelInfo*>(buildingModel)->m_nFlags.bIsRoad;
        if(!BuildingCandidate(plant->m_nType,false,road,buildingModel->m_szName,sizeof(buildingModel->m_szName))) return;
    }
    auto* frame=static_cast<RwFrame*>(plant->m_pRwObject->parent);
    if(!frame) return;
    for(const auto& saved:savedFrames) if(saved.frame==frame) return;
    const auto original=*RwFrameGetMatrix(frame);
    const auto world=*getLtm(frame);
    VegetationAnchor anchor;
    if(!vegetation) {
        SceneryBounds bounds;
        bool grounded=false;
        if(!street) {
            const auto found=groupedBuildings.find(plant);
            const auto& p=plant->GetMatrix().pos;
            if(found!=groupedBuildings.end()&&found->second.model==plant->m_nModelIndex&&found->second.object==plant->m_pRwObject&&found->second.position.x==p.x&&found->second.position.y==p.y&&found->second.position.z==p.z) {
                bounds=found->second.bounds;grounded=found->second.groundFound;
            }
        }
        if(!bounds.Valid()) SceneryMeshBounds(frame,bounds);
        if(!grounded&&bounds.Valid()) {float ground=bounds.z0;grounded=SceneryGroundPlane(bounds,ground);if(grounded) bounds.z0=ground;}
        // A missing/unloaded ground collision must not expose the world void.
        if(!grounded) {
            static std::array<bool,6500> logged{};
            if(!logged[plant->m_nModelIndex]){Log("scenery held still: ground unavailable for "+SceneryName(model->m_szName)+" model="+std::to_string(plant->m_nModelIndex));logged[plant->m_nModelIndex]=true;}
            return;
        }
        if(bounds.Valid()) {anchor.Add(RwV3d{bounds.x0,bounds.y0,bounds.z0});anchor.Add(RwV3d{bounds.x1,bounds.y1,bounds.z0});anchor.top=bounds.z1;}
    }
    if(!anchor.Valid()&&buildingModel&&buildingModel->m_pColModel) {
        // Static building bounds avoid traversing thousands of mesh vertices
        // every frame. Physics/collision data itself is never modified.
        const auto& box=buildingModel->m_pColModel->m_boundBox;
        for(float x:{box.m_vecMin.x,box.m_vecMax.x})
            for(float y:{box.m_vecMin.y,box.m_vecMax.y})
                for(float z:{box.m_vecMin.z,box.m_vecMax.z})
                    anchor.Add(RwV3d{world.pos.x+world.right.x*x+world.up.x*y+world.at.x*z,
                        world.pos.y+world.right.y*x+world.up.y*y+world.at.y*z,
                        world.pos.z+world.right.z*x+world.up.z*y+world.at.z*z});
    }
    if(!anchor.Valid()) VegetationBottom(frame,anchor);
    if(!anchor.Valid()) {
        static std::array<bool,6500> logged{};
        if(!logged[plant->m_nModelIndex]){Log("scenery held still: mesh bounds unavailable for "+SceneryName(model->m_szName)+" model="+std::to_string(plant->m_nModelIndex));logged[plant->m_nModelIndex]=true;}
        return;
    }
    const auto scale=vegetation?VegetationScale(phase,cfg,anchor.top-anchor.bottom):(street?StreetScale(phase,cfg,anchor.top-anchor.bottom):BuildingScale(phase,cfg,anchor.top-anchor.bottom));
    if(scale.vertical==1) return;
    const auto basis=anchor.Basis(world);
    const SceneryLighting light{basis,scale};
    native.Invoke(&light); // Light registration and visible mesh share this frame's transform.
    const auto deformedWorld=DeformedMatrix(world,basis,scale);
    auto deformedLocal=deformedWorld;
    auto* parent=static_cast<RwFrame*>(frame->object.parent);
    if(parent&&!VegetationLocalMatrix(deformedWorld,*getLtm(parent),deformedLocal)) return;
    savedFrames.push_back({frame,original,true});
    *RwFrameGetMatrix(frame)=deformedLocal;
    updateFrame(frame);getLtm(frame);
    if(vegetation) {
        static std::array<bool,6500> loggedModels{};
        const int index=plant->m_nModelIndex;
        if(!loggedModels[index]) {
            Log("plant bounce applied: "+SceneryName(model->m_szName)+" model="+std::to_string(index)+
                " x="+std::to_string(world.pos.x)+" y="+std::to_string(world.pos.y)+
                " height="+std::to_string(anchor.top-anchor.bottom));
            loggedModels[index]=true;
        }
    }
    if(!vegetation&&!street) {
        static std::array<bool,6500> loggedModels{};
        const int index=plant->m_nModelIndex;
        if(!loggedModels[index]) {
            Log("building bounce applied: "+SceneryName(model->m_szName)+" model="+std::to_string(index)+
                " ground="+std::to_string(anchor.bottom)+" top="+std::to_string(anchor.top));
            loggedModels[index]=true;
        }
    }
    static bool logged[3]{};
    const int kind=vegetation?0:(street?2:1);
    if(!logged[kind]){Log(vegetation?"vegetation uses world Z only, fixed XY, travel cap and parent-space correction":(street?"street objects use independent F3 grounded deformation":"buildings use independent F4 shared-group world-Z-only deformation"));logged[kind]=true;}
}
float GroundOffset(CVehicle* vehicle) {
    for(const auto& saved:groundOffsets) if(saved.vehicle==vehicle) return saved.z;
    GroundAnchor anchor;
    const auto& basis=vehicle->GetMatrix();
    const auto scale=ActiveScale();
    const int kind=TransportKind(vehicle);
    // Keep the pivot/waterline/flight altitude for boats and aircraft.
    if(kind!=VEHICLE_AUTOMOBILE&&kind!=VEHICLE_BIKE) return 0;
    if(vehicle->m_nVehicleClass==VEHICLE_AUTOMOBILE) {
        auto* automobile=static_cast<CAutomobile*>(vehicle);
        for(int node:{CAR_WHEEL_RF,CAR_WHEEL_RM,CAR_WHEEL_RB,CAR_WHEEL_LF,CAR_WHEEL_LM,CAR_WHEEL_LB})
            WheelGroundPoints(automobile->m_aCarNodes[node],basis,scale,anchor);
    } else if(vehicle->m_nVehicleClass==VEHICLE_BIKE) {
        auto* bike=static_cast<CBike*>(vehicle);
        for(int node:{BIKE_WHEEL_FRONT,BIKE_WHEEL_REAR}) WheelGroundPoints(bike->m_aBikeNodes[node],basis,scale,anchor);
    }
    const bool wheelGeometry=anchor.Valid();
    if(!anchor.Valid()&&vehicle->m_nModelIndex>=0&&vehicle->m_nModelIndex<6500) {
        auto* model=ModelInfo(vehicle);
        if(model&&model->m_pColModel) {
            const auto& box=model->m_pColModel->m_boundBox;
            for(float x:{box.m_vecMin.x,box.m_vecMax.x})
                for(float y:{box.m_vecMin.y,box.m_vecMax.y})
                    for(float z:{box.m_vecMin.z,box.m_vecMax.z}) {
                        RwV3d p{basis.pos.x+basis.right.x*x+basis.up.x*y+basis.at.x*z,
                            basis.pos.y+basis.right.y*x+basis.up.y*y+basis.at.y*z,
                            basis.pos.z+basis.right.z*x+basis.up.z*y+basis.at.z*z};
                        anchor.Add(p.z,DeformPosition(p,basis,scale).z);
                    }
        }
    }
    const float offset=anchor.Offset();
    groundOffsets.push_back({vehicle,offset});
    static bool logged=false;
    if(anchor.Valid()&&!logged){Log(wheelGeometry?"wheel mesh ground anchor active; tyre lower extent preserved":"ground anchor active using collision bounds fallback");logged=true;}
    return offset;
}
// This executable's HAnim layout differs from the legacy SDK header.
// Verified by GetMatrixArray (hierarchy+8) and IDGetIndex (numNodes+4).
struct SteamHierarchy { int flags,numNodes; RwMatrix* matrices; };
using GetHierarchyFn=SteamHierarchy*(__cdecl*)(RpClump*);
GetHierarchyFn getHierarchy=nullptr;
unsigned long rendered=0;
bool mismatchLogged=false;
void BeforeRender(CVehicle* vehicle) {
    if(!EffectActive(vehicle)) return;
    if(!vehicle->m_pRwObject||!RenderableObjectType(vehicle->m_pRwObject->type)) return;
    // Airtrain/its distant model can be an atomic rather than a clump.
    auto* frame=static_cast<RwFrame*>(vehicle->m_pRwObject->parent);
    if(!frame) return;
    for(const auto& saved:savedFrames) if(saved.frame==frame) return;
    const float groundOffset=GroundOffset(vehicle);
    savedFrames.push_back({frame,*RwFrameGetMatrix(frame)});
    const auto& saved=savedFrames.back();
    *RwFrameGetMatrix(frame)=ScaledMatrix(saved.original,ActiveScale());
    RwFrameGetMatrix(frame)->pos.z+=groundOffset;
    updateFrame(frame);getLtm(frame);
    if(rendered++==0) Log("first automobile rendered; full model and deferred atomics scaled");
    static std::array<bool,6> loggedTypes{};
    const int kind=TransportKind(vehicle);
    if(kind>=0&&kind<int(loggedTypes.size())&&!loggedTypes[kind]) {
        Log("transport kind "+std::to_string(kind)+" model "+std::to_string(vehicle->m_nModelIndex)+" rendered with bounce");loggedTypes[kind]=true;
    }
    static bool replayLogged=false;
    if(ReplayPlaying()&&!replayLogged){Log("replay automobile rendered with bounce active");replayLogged=true;}
}
template<class Basis> void DeformActor(CEntity* entity,const Basis& basis,float groundOffset) {
    if(!entity->m_pRwObject||!RenderableObjectType(entity->m_pRwObject->type)) return;
    auto* frame=static_cast<RwFrame*>(entity->m_pRwObject->parent);
    if(!frame) return;
    for(const auto& saved:savedFrames) if(saved.frame==frame) return;
    const auto scale=ActiveScale();
    savedFrames.push_back({frame,*RwFrameGetMatrix(frame)});
    *RwFrameGetMatrix(frame)=DeformedMatrix(savedFrames.back().original,basis,scale);
    RwFrameGetMatrix(frame)->pos.z+=groundOffset;
    updateFrame(frame);getLtm(frame);
    auto* hierarchy=entity->m_pRwObject->type==rpCLUMP?getHierarchy(entity->m_pRwClump):nullptr;
    if(hierarchy&&hierarchy->matrices&&hierarchy->numNodes>0&&hierarchy->numNodes<=128&&!(hierarchy->flags&0x4000)) {
        bool duplicate=false;
        for(const auto& saved:savedBones) if(saved.matrices==hierarchy->matrices) duplicate=true;
        if(!duplicate) {
            savedBones.push_back({hierarchy->matrices,{hierarchy->matrices,hierarchy->matrices+hierarchy->numNodes}});
            for(int i=0;i<hierarchy->numNodes;++i) {
                hierarchy->matrices[i]=DeformedMatrix(savedBones.back().originals[i],basis,scale);
                hierarchy->matrices[i].pos.z+=groundOffset;
            }
        }
    }
}
float ActorGroundOffset(CEntity* actor,const CMatrix& basis) {
    if(!actor->m_pRwObject||actor->m_pRwObject->type!=rpCLUMP) return 0;
    auto* hierarchy=getHierarchy(actor->m_pRwClump);
    if(!hierarchy||!hierarchy->matrices||hierarchy->numNodes<=0||hierarchy->numNodes>128||hierarchy->flags&0x4000) return 0;
    GroundAnchor anchor;
    const auto scale=ActiveScale();
    const RwMatrix* originals=hierarchy->matrices;
    for(const auto& saved:savedBones) if(saved.matrices==hierarchy->matrices) {
        originals=saved.originals.data();break;
    }
    for(int i=0;i<hierarchy->numNodes;++i) {
        const auto& p=originals[i].pos;
        anchor.Add(p.z,DeformPosition(p,basis,scale).z);
    }
    return anchor.Offset();
}
void DeformStandingActor(CEntity* actor) {
    // Keep the actor's X/Y pivot and lowest skeleton point fixed in world space.
    const auto& basis=actor->GetMatrix();
    DeformActor(actor,basis,ActorGroundOffset(actor,basis));
}
struct ActorBounds {
    RwV3d min{INFINITY,INFINITY,INFINITY},max{-INFINITY,-INFINITY,-INFINITY};
    void Add(RwV3d p) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) return;
        min.x=std::min(min.x,p.x);min.y=std::min(min.y,p.y);min.z=std::min(min.z,p.z);
        max.x=std::max(max.x,p.x);max.y=std::max(max.y,p.y);max.z=std::max(max.z,p.z);
    }
    bool Valid() const {return std::isfinite(min.x);}
    RwV3d Center() const {return {(min.x+max.x)*.5f,(min.y+max.y)*.5f,(min.z+max.z)*.5f};}
};
void ActorGeometryBounds(RwFrame* frame,ActorBounds& bounds,int depth=0) {
    if(!frame||depth>=32) return;
    const auto* world=getLtm(frame);
    auto* sentinel=&frame->objectList.link;
    int countObjects=0;
    for(auto* link=sentinel->next;link!=sentinel&&countObjects++<64;link=link->next) {
        auto* object=rwLLLinkGetData(link,RwObjectHasFrame,lFrame);
        if(object->object.type!=rpATOMIC||!(object->object.flags&rpATOMICRENDER)) continue;
        auto* atomic=reinterpret_cast<RpAtomic*>(object);
        if(!atomic->geometry) continue;
        const auto* geometry=reinterpret_cast<const unsigned char*>(atomic->geometry);
        const int count=*reinterpret_cast<const int*>(geometry+0x14);
        const int targets=*reinterpret_cast<const int*>(geometry+0x18);
        const auto* morph=*reinterpret_cast<RpMorphTarget*const*>(geometry+0x5C);
        if(count<=0||count>32768||targets<=0||!morph||!morph->verts) continue;
        for(int i=0;i<count;++i) {
            const auto& p=morph->verts[i];
            bounds.Add({world->pos.x+world->right.x*p.x+world->up.x*p.y+world->at.x*p.z,
                        world->pos.y+world->right.y*p.x+world->up.y*p.y+world->at.y*p.z,
                        world->pos.z+world->right.z*p.x+world->up.z*p.y+world->at.z*p.z});
        }
    }
    for(auto* child=frame->child;child;child=child->next) ActorGeometryBounds(child,bounds,depth+1);
}
struct ActorTransform { RwMatrix basis{};float offset=0;bool humanoid=false; };
struct CutsceneTransform { CCutsceneObject* actor;ActorTransform transform; };
std::vector<CutsceneTransform> cutsceneTransforms;
ActorTransform OwnCutsceneTransform(CCutsceneObject* actor) {
    for(const auto& cached:cutsceneTransforms) if(cached.actor==actor) return cached.transform;
    ActorTransform transform;
    if(!actor->m_pRwObject||!RenderableObjectType(actor->m_pRwObject->type)) return transform;
    auto* frame=static_cast<RwFrame*>(actor->m_pRwObject->parent);
    if(!frame) return transform;
    auto* hierarchy=actor->m_pRwObject->type==rpCLUMP?getHierarchy(actor->m_pRwClump):nullptr;
    const bool worldBones=hierarchy&&hierarchy->matrices&&hierarchy->numNodes>0&&
        hierarchy->numNodes<=128&&!(hierarchy->flags&0x4000);
    // Cutscene entities may all sit at the scene origin. Their animated root
    // bone, not CEntity::GetMatrix(), carries the moving actor's world pivot.
    RwV3d pivot=getLtm(frame)->pos;
    const RwMatrix* bones=worldBones?hierarchy->matrices:nullptr;
    if(worldBones) {
        for(const auto& saved:savedBones) if(saved.matrices==bones) {bones=saved.originals.data();break;}
        pivot=bones[0].pos;
    }
    else {
        ActorBounds bounds;ActorGeometryBounds(frame,bounds);
        if(bounds.Valid()) pivot=bounds.Center();
    }
    const auto basis=UprightActorBasis(*getLtm(frame),pivot);
    const bool humanoid=worldBones&&hierarchy->numNodes>=15;
    // Props and aircraft keep their animated pivot/altitude. Humanoid skeletons
    // preserve their own lowest point, independently of any scene attachment.
    float offset=0;
    if(humanoid) {
        GroundAnchor anchor;
        for(int i=0;i<hierarchy->numNodes;++i) {
            const auto p=bones[i].pos;
            anchor.Add(p.z,DeformPosition(p,basis,ActiveScale()).z);
        }
        offset=anchor.Offset();
    }
    transform={basis,offset,humanoid};
    cutsceneTransforms.push_back({actor,transform});
    return transform;
}
CCutsceneObject* CutsceneAttachmentOwner(CCutsceneObject* actor) {
    if(!actor->m_pAttachTo) return nullptr;
    // Steam array/count verified in StartCutscene, not the SDK's 1.0 globals.
    const int count=*reinterpret_cast<const int*>(GameAddress(0xA0EC7C));
    if(count<=0||count>50) return nullptr;
    auto** actors=reinterpret_cast<CCutsceneObject**>(GameAddress(0x937290));
    for(int i=0;i<count;++i) {
        auto* owner=actors[i];
        if(!owner||owner==actor) continue;
        if(actor->m_pAttachmentObject==owner) return owner;
        // Frame-based attachments do not store m_pAttachmentObject. Locate
        // the owner by walking its actual frame tree, never by spatial proximity.
        if(!actor->m_pAttachmentObject&&owner->m_pRwObject) {
            auto* root=static_cast<RwFrame*>(owner->m_pRwObject->parent);
            auto* attached=actor->m_pAttachTo;
            for(int depth=0;attached&&depth<32;++depth) {
                if(attached==root) return owner;
                attached=static_cast<RwFrame*>(attached->object.parent);
            }
        }
    }
    return nullptr;
}
void DeformCutsceneActor(CCutsceneObject* actor) {
    if(!actor->m_pRwObject||!RenderableObjectType(actor->m_pRwObject->type)) return;
    auto* frame=static_cast<RwFrame*>(actor->m_pRwObject->parent);
    if(!frame) return;
    for(const auto& saved:savedFrames) if(saved.frame==frame) return;
    auto transform=OwnCutsceneTransform(actor);
    bool held=false;
    // Only a real attachment to a humanoid shares the owner's transform.
    // Independent actors, furniture and aircraft retain their own pivots.
    if(!transform.humanoid) {
        auto* owner=CutsceneAttachmentOwner(actor);
        if(owner) {
            const auto parent=OwnCutsceneTransform(owner);
            if(parent.humanoid) {transform=parent;held=true;}
        }
    }
    if(!cfg.enabled&&!transform.humanoid) return;
    DeformActor(actor,transform.basis,transform.offset);
    static std::vector<int> loggedModels;
    if(std::find(loggedModels.begin(),loggedModels.end(),actor->m_nModelIndex)==loggedModels.end()) {
        loggedModels.push_back(actor->m_nModelIndex);
        Log("cutscene own animated pivot model="+std::to_string(actor->m_nModelIndex)+
            " grounded="+std::to_string(transform.humanoid)+" held="+std::to_string(held));
    }
}
CVehicle* PedBounceVehicle(CPed* ped) {
    if(!ped) return nullptr;
    if(ped->m_bInVehicle) return ped->m_pVehicle;
    // Vehicle animations can be freed while this field still contains the old pointer.
    // Do not even read the association outside the interaction states.
    if(!VehicleInteractionState(ped->m_ePedState)) return nullptr;
    const auto* association=static_cast<const CAnimBlendAssociation*>(ped->m_pVehicleAnim);
    // The alignment/walk-to-door animation is deliberately excluded.
    if(!association||!DoorInteractionActive(ped->m_ePedState,association->m_nAnimId)) return nullptr;
    return ped->m_pVehicle?ped->m_pVehicle:ped->m_pObjectiveVehicle;
}
void BeforePedRender(CPed* ped) {
    DiagnosticScope diagnostic("ped render",ped?ped->m_nModelIndex:-1);
    if(!ped||cfg.amplitude<=0||Paused()) return;
    auto* vehicle=PedBounceVehicle(ped);
    const int effect=PedEffect(cfg.allPeople,EffectActive(vehicle),ped->m_bInVehicle);
    if(!effect) return;
    // CPed::Render copies the complete hand matrix to the weapon frame after
    // this hook (Steam 0x4FE11D..0x4FE17F). Do not scale the weapon a second time.
    if(effect==2) DeformActor(ped,vehicle->GetMatrix(),GroundOffset(vehicle));
    else DeformStandingActor(ped);
    static bool logged=false;
    if(!logged){Log("first person rendered; skeleton and held weapon hand matrix deformed");logged=true;}
    static bool doorLogged=false;
    if(!ped->m_bInVehicle&&EffectActive(vehicle)&&!doorLogged){Log("door contact/entry actor rendered before in-vehicle flag");doorLogged=true;}
}
void __fastcall RenderCutscene(CCutsceneObject* actor,void*) {
    DiagnosticScope diagnostic("cutscene render",actor?actor->m_nModelIndex:-1);
    if((cfg.enabled||cfg.allPeople)&&cfg.amplitude>0&&!Paused()) {
        DeformCutsceneActor(actor);
        static bool logged=false;
        if(!logged){Log("cutscene actors and attached props rendered with bounce");logged=true;}
    }
    originalCutsceneRender(actor);
}
void RestoreFrames() {
    DiagnosticScope diagnostic("restore render frames");
    for(const auto& saved:savedBones)
        std::memcpy(saved.matrices,saved.originals.data(),saved.originals.size()*sizeof(RwMatrix));
    savedBones.clear();
    for(const auto& saved:savedFrames) {
        auto* matrix=RwFrameGetMatrix(saved.frame);
        *matrix=saved.original;
        updateFrame(saved.frame);getLtm(saved.frame);
        if(std::memcmp(matrix,&saved.original,sizeof(RwMatrix))!=0&&!mismatchLogged) {
            Log("ERROR: root matrix changed during restoration");mismatchLogged=true;
        }
    }
    savedFrames.clear();
    groundOffsets.clear();
    cutsceneTransforms.clear();
}
void* __fastcall DeleteCutscene(CCutsceneObject* actor,void*,unsigned flags) {
    RestoreFrames(); // Drop all borrowed RW/bone pointers while owners are still alive.
    return originalCutsceneDelete(actor,flags);
}
void __fastcall DeleteCutsceneRw(CCutsceneObject* actor,void*) {
    RestoreFrames();
    originalCutsceneDeleteRw(actor);
}
void __fastcall RenderTransport(CVehicle* vehicle,void*) {
    DiagnosticScope diagnostic("transport render",vehicle?vehicle->m_nModelIndex:-1);
    BeforeRender(vehicle);
    auto* previous=lightingVehicle;
    lightingVehicle=EffectActive(vehicle)?vehicle:nullptr;
    originalRenders[vehicle->m_nVehicleClass](vehicle);
    lightingVehicle=previous;
}
void __fastcall PreRenderTransport(CVehicle* vehicle,void*) {
    DiagnosticScope diagnostic("transport pre-render",vehicle?vehicle->m_nModelIndex:-1);
    auto* previous=lightingVehicle;
    lightingVehicle=EffectActive(vehicle)?vehicle:nullptr;
    originalPreRenders[vehicle->m_nVehicleClass](vehicle);
    lightingVehicle=previous;
}
template<class Function> Function GameFunction(uintptr_t address) {
    return reinterpret_cast<Function>(GameAddress(address));
}
CVector LightPosition(CVector position) {
    if(lightingVehicle) {
        position=DeformPosition(position,lightingVehicle->GetMatrix(),ActiveScale());
        position.z+=GroundOffset(lightingVehicle);
    }
    else if(lightingScenery) position=DeformPosition(position,lightingScenery->basis,lightingScenery->scale);
    return position;
}
CVector LightDirection(CVector direction) {
    if(lightingVehicle) return DeformDirection(direction,lightingVehicle->GetMatrix(),ActiveScale());
    return lightingScenery?DeformDirection(direction,lightingScenery->basis,lightingScenery->scale):direction;
}
float LightRadius(float radius) { return lightingVehicle?radius*ActiveScale().horizontal:radius; }
void __cdecl RegisterCorona(unsigned id,unsigned char r,unsigned char g,unsigned char b,unsigned char alpha,
    const CVector& position,float radius,float farClip,unsigned char type,unsigned char flare,unsigned char reflection,
    unsigned char check,unsigned char streak,float angle,bool longDistance,float nearClip) {
    using Fn=decltype(&RegisterCorona);
    const auto transformed=LightPosition(position);
    GameFunction<Fn>(0x542690)(id,r,g,b,alpha,transformed,LightRadius(radius),farClip,type,flare,reflection,check,streak,angle,longDistance,nearClip);
    static bool logged=false;if(lightingVehicle&&!logged){Log("first automobile light registered; corona positions and size deformed");logged=true;}
}
void __cdecl UpdateCorona(unsigned id,const CVector& position,float farClip,float angle) {
    const auto transformed=LightPosition(position);
    GameFunction<decltype(&UpdateCorona)>(0x5422D0)(id,transformed,farClip,angle);
}
void __cdecl RegisterTexturedCorona(unsigned id,unsigned char r,unsigned char g,unsigned char b,unsigned char alpha,
    const CVector& position,float radius,float farClip,RwTexture* texture,unsigned char flare,unsigned char reflection,
    unsigned char check,unsigned char streak,float angle,bool longDistance,float nearClip) {
    GameFunction<decltype(&RegisterTexturedCorona)>(0x542380)(id,r,g,b,alpha,LightPosition(position),radius,farClip,texture,flare,reflection,check,streak,angle,longDistance,nearClip);
}
struct GameVector {float x,y,z;};
CVector ToCVector(GameVector v){return CVector(v.x,v.y,v.z);}
GameVector ToGameVector(const CVector& v){return {v.x,v.y,v.z};}
void __cdecl AddPointLight(unsigned char type,GameVector position,GameVector direction,float range,float r,float g,float b,unsigned char fog,bool shadows) {
    GameFunction<decltype(&AddPointLight)>(0x5675F0)(type,ToGameVector(LightPosition(ToCVector(position))),ToGameVector(LightDirection(ToCVector(direction))),LightRadius(range),r,g,b,fog,shadows);
}
void __cdecl RegisterBrightLight(GameVector position,GameVector right,GameVector up,GameVector at,unsigned char r,unsigned char g,unsigned char b,unsigned char alpha) {
    GameFunction<decltype(&RegisterBrightLight)>(0x571710)(ToGameVector(LightPosition(ToCVector(position))),ToGameVector(LightDirection(ToCVector(right))),ToGameVector(LightDirection(ToCVector(up))),ToGameVector(LightDirection(ToCVector(at))),r,g,b,alpha);
}
void __cdecl StoreLightShadow(CVehicle* vehicle,int id,RwTexture* texture,CVector* position,float frontX,float frontY,float sideX,float sideY,
    unsigned char r,unsigned char g,unsigned char b,float maxAngle) {
    auto transformed=LightPosition(*position);
    transformed.z=position->z; // The footprint remains projected onto the road.
    const auto front=LightDirection(CVector(frontX,frontY,0));
    const auto side=LightDirection(CVector(sideX,sideY,0));
    GameFunction<decltype(&StoreLightShadow)>(0x56DBC0)(vehicle,id,texture,&transformed,front.x,front.y,side.x,side.y,r,g,b,maxAngle);
    static bool logged=false;if(lightingVehicle&&!logged){Log("first automobile projected light registered; road footprint deformed");logged=true;}
}
template<class Function> struct GameCallable;
template<class Result,class... Arguments> struct GameCallable<Result(__cdecl*)(Arguments...)> {
    static constexpr bool value=((std::is_reference_v<Arguments>||std::is_trivially_copyable_v<Arguments>)&&...);
};
static_assert(sizeof(GameVector)==3*sizeof(float)&&std::is_trivially_copyable_v<GameVector>);
static_assert(GameCallable<decltype(&RegisterCorona)>::value&&GameCallable<decltype(&RegisterTexturedCorona)>::value&&
    GameCallable<decltype(&UpdateCorona)>::value&&GameCallable<decltype(&AddPointLight)>::value&&
    GameCallable<decltype(&RegisterBrightLight)>::value&&GameCallable<decltype(&StoreLightShadow)>::value,
    "Game entry points must use plain by-value data or references");
uintptr_t LightReplacement(uintptr_t target) {
    if(target==AddressForVersion(0x542690,retailExecutable))return reinterpret_cast<uintptr_t>(&RegisterCorona);
    if(target==AddressForVersion(0x542380,retailExecutable))return reinterpret_cast<uintptr_t>(&RegisterTexturedCorona);
    if(target==AddressForVersion(0x5422D0,retailExecutable))return reinterpret_cast<uintptr_t>(&UpdateCorona);
    if(target==AddressForVersion(0x5675F0,retailExecutable))return reinterpret_cast<uintptr_t>(&AddPointLight);
    if(target==AddressForVersion(0x571710,retailExecutable))return reinterpret_cast<uintptr_t>(&RegisterBrightLight);
    if(target==AddressForVersion(0x56DBC0,retailExecutable))return reinterpret_cast<uintptr_t>(&StoreLightShadow);
    return 0;
}
bool CallMatches(uintptr_t address,uintptr_t target) {
    auto* code=reinterpret_cast<const unsigned char*>(GameAddress(address));
    int32_t rel=0;std::memcpy(&rel,code+1,sizeof(rel));
    return code[0]==0xE8&&uintptr_t(code+5)+rel==GameAddress(target);
}

bool BytesMatch(uintptr_t address,const unsigned char* expected,size_t size) {
    return std::memcmp(reinterpret_cast<const void*>(GameAddress(address)),expected,size)==0;
}
bool CheckSteam() {
    if(plugin::GetGameVersion()!=GAME_STEAM) { Log("unsupported executable: this build targets GAME_STEAM only");return false; }
    constexpr unsigned char verticalTraceSignature[]={0x53,0x56,0x55,0x83,0xEC,0x40};
    if(!BytesMatch(0x4D89C0,verticalTraceSignature,sizeof(verticalTraceSignature))) {Log("ground trace signature mismatch; hooks disabled");return false;}
    constexpr unsigned char buildingPoolSignature[]={0x8B,0x0D,0x48,0xE2,0x97,0};
    constexpr unsigned char treadablePoolSignature[]={0x8B,0x0D,0x3C,0x78,0x97,0};
    if(!BytesMatch(0x407D70,buildingPoolSignature,sizeof(buildingPoolSignature))||!BytesMatch(0x407D4B,treadablePoolSignature,sizeof(treadablePoolSignature))) {
        Log("building pool signatures differ from verified Steam executable; hooks disabled");return false;
    }
    for(auto table:vegetationVtables) {
        auto* slots=reinterpret_cast<const uintptr_t*>(GameAddress(table));
        if(slots[12]!=GameAddress(0x4886F0)) {
            Log("vegetation PreRender vtable differs from verified Steam executable; hooks disabled");return false;
        }
    }
    constexpr unsigned char treeWindSignature[]={0x53,0x89,0xCB,0x83,0xEC,0x50};
    if(!CallMatches(0x488838,0x487900)||!CallMatches(0x487B0E,0x629950)||
       !BytesMatch(0x487900,treeWindSignature,sizeof(treeWindSignature))) {
        Log("vegetation wind callsite differs from verified Steam executable; hooks disabled");return false;
    }
    for(const auto& transport:transportHooks) {
        auto* slots=reinterpret_cast<const uintptr_t*>(GameAddress(transport.vtable));
        if(slots[12]!=GameAddress(transport.preRender)||slots[13]!=GameAddress(transport.render)) {
            Log("transport vtable differs from verified Steam executable; hooks disabled");return false;
        }
    }
    auto* cutsceneSlots=reinterpret_cast<const uintptr_t*>(GameAddress(cutsceneVtable));
    constexpr unsigned char cutsceneRunningSignature[]={0xC6,0x05,0xBA,0xFA,0xA0,0,1};
    constexpr unsigned char pedAssociationSignature[]={0x8B,0x8B,0xF8,0x01,0,0};
    constexpr unsigned char cutsceneCountSignature[]={0x8B,0x1D,0x7C,0xEC,0xA0,0};
    constexpr unsigned char cutsceneArraySignature[]={0x8B,0x0C,0x9D,0x90,0x72,0x93,0};
    constexpr unsigned char attachmentSignature[]={0x89,0xAB,0x9C,0x01,0,0};
    constexpr unsigned char weaponCopySignature[]={0xD9,0x06,0x8D,0x50,0x10,0x50,0xD9,0x1A};
    if(cutsceneSlots[2]!=GameAddress(0x4E0400)||
       cutsceneSlots[6]!=GameAddress(0x4895A0)||
       cutsceneSlots[13]!=GameAddress(cutsceneRender)||
       !CallMatches(0x4A5C70,0x4A42D0)||
       !CallMatches(0x4E0000,0x4E2630)||
       !BytesMatch(0x40650D,cutsceneRunningSignature,sizeof(cutsceneRunningSignature))||
       !BytesMatch(0x517104,pedAssociationSignature,sizeof(pedAssociationSignature))||
       !BytesMatch(0x4063B8,cutsceneCountSignature,sizeof(cutsceneCountSignature))||
       !BytesMatch(0x406402,cutsceneArraySignature,sizeof(cutsceneArraySignature))||
       !BytesMatch(0x406640,attachmentSignature,sizeof(attachmentSignature))||
       !BytesMatch(0x4FE11D,weaponCopySignature,sizeof(weaponCopySignature))) {
        Log("cutscene/door interaction signature mismatch; hooks disabled");return false;
    }
    if(!CallMatches(0x4FE0D6,0x4886B0)) {
        Log("PreRender/occupant render differs from verified executable; hooks disabled");return false;
    }
    for(const auto& site:lightCallsites) if(!CallMatches(site.address,site.target)) {
        Log("automobile light callsite mismatch; hooks disabled");return false;
    }
    constexpr unsigned char hierarchySignature[]={0x83,0xEC,0x08,0xC7,0x44,0x24,0x04,0,0,0,0};
    constexpr unsigned char boneArraySignature[]={0x8B,0x44,0x24,0x04,0x8B,0x40,0x08,0xC3};
    if(!BytesMatch(0x57F080,hierarchySignature,sizeof(hierarchySignature))||!BytesMatch(0x645320,boneArraySignature,sizeof(boneArraySignature))) {
        Log("Steam skeletal hierarchy mismatch; hooks disabled");return false;
    }
    constexpr unsigned char verticesSignature[]={0x8B,0x46,0x14};
    constexpr unsigned char morphSignature[]={0x8B,0x46,0x5C};
    if(!BytesMatch(0x64B9FD,verticesSignature,sizeof(verticesSignature))||!BytesMatch(0x64BA19,morphSignature,sizeof(morphSignature))) {
        Log("Steam wheel geometry layout mismatch; hooks disabled");return false;
    }
    constexpr unsigned char renderSignature[]={0x53,0x56,0x89,0xCB,0x57,0x55,0x83,0xEC,0x18};
    constexpr unsigned char drawingSignature[]={0xE8,0x75,0xA7,0x17,0};
    constexpr unsigned char menuBeforeGame[]={0xE8,0x5B,0xDA,0xFF,0xFF};
    constexpr unsigned char menuDuringGame[]={0xE8,0x60,0xD6,0xFF,0xFF};
    constexpr unsigned char replaySignature[]={0xC6,0x05,0xF6,0xFA,0xA0,0,1};
    if(!BytesMatch(0x5895C0,renderSignature,sizeof(renderSignature))||
       !BytesMatch(0x4A60A6,drawingSignature,sizeof(drawingSignature))||
       !BytesMatch(0x4A5C03,menuBeforeGame,sizeof(menuBeforeGame))||
       !BytesMatch(0x4A5FFE,menuDuringGame,sizeof(menuDuringGame))||
       !BytesMatch(0x4D77FB,replaySignature,sizeof(replaySignature))) {
        Log("full render/drawing/replay signature mismatch; hooks disabled");return false;
    }
    constexpr unsigned char updateSignature[]={0x8B,0x44,0x24,0x04,0x33,0xC9,0x8B,0x90,0xA0,0,0,0,0x8A,0x4A,0x03};
    constexpr unsigned char ltmSignature[]={0x56,0x8B,0x74,0x24,0x08,0x8B,0x86,0xA0,0,0,0,0xF6,0x40,0x03,0x01};
    if(!BytesMatch(0x643CB0,updateSignature,sizeof(updateSignature))||!BytesMatch(0x643D30,ltmSignature,sizeof(ltmSignature))) {
        Log("RenderWare signature mismatch; hooks disabled");return false;
    }
    updateFrame=reinterpret_cast<UpdateFrameFn>(GameAddress(0x643CB0));
    getLtm=reinterpret_cast<GetLtmFn>(GameAddress(0x643D30));
    getHierarchy=GameFunction<GetHierarchyFn>(0x57F080);
    return true;
}

struct ProcessMemory {
    const unsigned char* At(uintptr_t a)const{return reinterpret_cast<const unsigned char*>(plugin::GetGlobalAddress(a));}
    uintptr_t Abs(uintptr_t a)const{return plugin::GetGlobalAddress(a);}
};
bool CheckGame(){
    const auto version=plugin::GetGameVersion();
    if(version!=GAME_STEAM&&version!=GAME_10EN){Log("unsupported executable; requires Steam or 1.0 EN");return false;}
    retailExecutable=version==GAME_10EN;layout=retailExecutable?&layout10EN:&layoutSteam;
    lightCallsites.assign(layout->lightCallsites.begin(),layout->lightCallsites.end());
    const auto extra=retailExecutable?Whole(sceneryLightsRetail):Whole(sceneryLightsSteam);
    lightCallsites.insert(lightCallsites.end(),extra.begin(),extra.end());
    if(!retailExecutable)return CheckSteam();
    std::string failure;
    if(!VerifyLayout(*layout,ProcessMemory{},failure)){Log(failure);return false;}
    const VehicleHook hooks[]={layout->automobile,layout->boat,layout->heli,layout->plane,layout->bike};
    for(size_t i=0;i<5;++i){transportHooks[i].vtable=hooks[i].vtable;transportHooks[i].preRender=hooks[i].preRender;transportHooks[i].render=hooks[i].render;}
    cutsceneVtable=layout->cutsceneVtable;cutsceneRender=layout->cutsceneRender;
    std::copy(std::begin(sceneryVtablesRetail),std::end(sceneryVtablesRetail),std::begin(vegetationVtables));
    for(auto v:vegetationVtables)if(*reinterpret_cast<const uintptr_t*>(GameAddress(v+48))!=GameAddress(0x4886F0)){Log("retail scenery vtable mismatch");return false;}
    for(auto s:extra)if(!CallMatches(s.address,s.target)){Log("retail scenery light call mismatch");return false;}
    const unsigned char trace[]={0x53,0x56,0x55,0x83,0xEC,0x40};
    const unsigned char wind[]={0x53,0x89,0xCB,0x83,0xEC,0x50};
    const unsigned char buildings[]={0x8B,0x0D,0x40,0xF2,0x97,0};
    const unsigned char treadables[]={0x8B,0x0D,0x34,0x88,0x97,0};
    if(!BytesMatch(0x4D8B00,trace,sizeof(trace))||!BytesMatch(0x407D70,buildings,sizeof(buildings))||!BytesMatch(0x407D4B,treadables,sizeof(treadables))||!BytesMatch(0x487A20,wind,sizeof(wind))||!CallMatches(0x488958,0x487A20)||!CallMatches(0x487C2E,0x629C90)||!CallMatches(0x4A5D33,0x4A37A4)||!CallMatches(0x4A612E,0x4A37A4)){
        Log("retail scenery/menu signature mismatch");return false;
    }
    updateFrame=reinterpret_cast<UpdateFrameFn>(GameAddress(0x643CB0));
    getLtm=reinterpret_cast<GetLtmFn>(GameAddress(0x643D30));
    getHierarchy=GameFunction<GetHierarchyFn>(0x57F080);
    return true;
}

struct ViceBounce {
    ViceBounce() {
        char module[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase),module,MAX_PATH);
        std::string dir(module);dir=dir.substr(0,dir.find_last_of("\\/")+1);
        iniPath=dir+"ViceBounce.ini";logPath=dir+"ViceBounce.log";
#ifdef VICEBOUNCE_DIAGNOSTICS
        diag::Install(logPath);
#endif
        exceptionLog=CreateFileA((dir+"ViceBounce-crash.log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        AddVectoredExceptionHandler(0,RecordException);
        Log(std::string("ViceBounce 0.19-dev startup; ")+plugin::GetGameVersionName());
        if(!CheckGame()) return;
        LoadConfig();
        savedFrames.reserve(256);
        savedBones.reserve(64);
        groundOffsets.reserve(256);
        cutsceneTransforms.reserve(50);
        // Prepare writable pages before changing any pointer or callsite.
        struct Writable { void* address; size_t size; DWORD protection; };
        std::vector<Writable> writable;
        for(const auto& transport:transportHooks)
            writable.push_back({reinterpret_cast<void*>(GameAddress(transport.vtable+12*sizeof(RenderFn))),2*sizeof(RenderFn),0});
        writable.push_back({reinterpret_cast<void*>(GameAddress(cutsceneVtable+2*sizeof(uintptr_t))),12*sizeof(uintptr_t),0});
        for(const auto& site:lightCallsites) writable.push_back({reinterpret_cast<void*>(GameAddress(site.address)),5,0});
        for(auto table:vegetationVtables) writable.push_back({reinterpret_cast<void*>(GameAddress(table+12*sizeof(uintptr_t))),sizeof(uintptr_t),0});
        size_t prepared=0;
        for(auto& patch:writable) {
            if(!VirtualProtect(patch.address,patch.size,PAGE_EXECUTE_READWRITE,&patch.protection)) break;
            ++prepared;
        }
        if(prepared!=writable.size()) {
            while(prepared>0){auto& patch=writable[--prepared];DWORD ignored;VirtualProtect(patch.address,patch.size,patch.protection,&ignored);}
            Log("cannot prepare light/render patches; hooks disabled");return;
        }
        for(const auto& transport:transportHooks) {
            auto* slots=reinterpret_cast<RenderFn*>(GameAddress(transport.vtable));
            originalRenders[transport.runtimeType]=slots[13];
            originalPreRenders[transport.runtimeType]=slots[12];
            slots[13]=reinterpret_cast<RenderFn>(&RenderTransport);
            slots[12]=reinterpret_cast<RenderFn>(&PreRenderTransport);
        }
        auto* deleteSlot=reinterpret_cast<CutsceneDeleteFn*>(GameAddress(cutsceneVtable+2*sizeof(uintptr_t)));
        originalCutsceneDelete=*deleteSlot;*deleteSlot=reinterpret_cast<CutsceneDeleteFn>(&DeleteCutscene);
        auto* deleteRwSlot=reinterpret_cast<CutsceneDeleteRwFn*>(GameAddress(cutsceneVtable+6*sizeof(uintptr_t)));
        originalCutsceneDeleteRw=*deleteRwSlot;*deleteRwSlot=reinterpret_cast<CutsceneDeleteRwFn>(&DeleteCutsceneRw);
        auto* cutsceneSlot=reinterpret_cast<CutsceneRenderFn*>(GameAddress(cutsceneVtable+13*sizeof(CutsceneRenderFn)));
        originalCutsceneRender=*cutsceneSlot;
        *cutsceneSlot=reinterpret_cast<CutsceneRenderFn>(&RenderCutscene);
        for(const auto& site:lightCallsites) {
            auto* code=reinterpret_cast<unsigned char*>(GameAddress(site.address));
            const int32_t rel=static_cast<int32_t>(LightReplacement(site.target)-uintptr_t(code+5));
            std::memcpy(code+1,&rel,sizeof(rel));
        }
        for(auto table:vegetationVtables) {
            auto* slot=reinterpret_cast<uintptr_t*>(GameAddress(table+12*sizeof(uintptr_t)));
            *slot=reinterpret_cast<uintptr_t>(&VegetationPreRender);
        }
        for(auto i=writable.rbegin();i!=writable.rend();++i){DWORD ignored;VirtualProtect(i->address,i->size,i->protection,&ignored);}
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);
        plugin::Events::pedRenderEvent.before+=BeforePedRender;
        plugin::Events::drawingEvent.before+=Tick;
        steamMenuDrawingEvent.before+=MusicPickerControls;
        Log("F11 music picker enabled in gameplay, pause and main menu");
        plugin::Events::gameProcessEvent.before+=[]{
            RestoreFrames();
            sceneryGroundQueriesAllowed=true;
            auto pending=std::move(pendingGroundQueries);pendingGroundQueries.clear();
            for(const auto& bounds:pending){float ground=bounds.z0;SceneryGroundPlane(bounds,ground);}
            RefreshSceneryGroups();
            sceneryGroundQueriesAllowed=false;
        };
        Log(std::string("verified ")+layout->name+" full Render/PreRender for cars, bikes, boats, helicopters and planes; occupants, "+std::to_string(std::size(lightCallsites))+" light callsites, replay, cutscenes, atomic aircraft, standing actor anchors and cutscene destruction guards enabled");
    }
} instance;
}
