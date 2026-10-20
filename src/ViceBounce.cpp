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
#include <windows.h>
#include <array>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdio>
#include <vector>
#include "LightCallsites.h"
#include "GameLayout.h"
#include "GameLayoutCheck.h"
#include "Diagnostics.h"
#include "MusicPlayback.h"
#include "BeatMap.h"
#include "MusicImporter.h"
#include <filesystem>

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace {
using namespace vicebounce;
Settings cfg;
double phase=0;
std::string iniPath,logPath;
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
// Addresses of the running executable. Set by CheckGame() once the whole table has been verified
// against the game's own bytes; nothing below runs before that.
const GameLayout* layout=nullptr;
std::array<TransportHook,5> transportHooks{};
using CutsceneRenderFn=void(__thiscall*)(CCutsceneObject*);
CutsceneRenderFn originalCutsceneRender=nullptr;
using CutsceneDeleteFn=void*(__thiscall*)(CCutsceneObject*,unsigned);
CutsceneDeleteFn originalCutsceneDelete=nullptr;
using CutsceneDeleteRwFn=void(__thiscall*)(CCutsceneObject*);
CutsceneDeleteRwFn originalCutsceneDeleteRw=nullptr;
CVehicle* lightingVehicle=nullptr;
CVehicleModelInfo* ModelInfo(CVehicle* vehicle) {
    if(!vehicle||vehicle->m_nModelIndex<0||vehicle->m_nModelIndex>=6500) return nullptr;
    auto** models=reinterpret_cast<CVehicleModelInfo**>(plugin::GetGlobalAddress(layout->modelInfoPtrs));
    auto* model=models[vehicle->m_nModelIndex];
    // Airtrain LOD is a SIMPLE model: never read the vehicle-only tail there.
    return model&&model->m_nType==MODEL_INFO_VEHICLE?model:nullptr;
}
int TransportKind(CVehicle* vehicle) {
    if(auto* model=ModelInfo(vehicle)) return model->m_nVehicleType;
    return vehicle->m_nVehicleClass;
}

bool ReplayPlaying() {
    // CReplay::Mode, per executable (the SDK's own CReplay::Mode only matches 1.0).
    return *reinterpret_cast<const unsigned char*>(plugin::GetGlobalAddress(layout->replayMode))==1;
}
bool CutscenePlaying() {
    // CCutsceneMgr::ms_running; StartCutscene writes this byte at 0x40650D in both executables.
    return *reinterpret_cast<const unsigned char*>(plugin::GetGlobalAddress(layout->cutsceneRunning))!=0;
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
    std::snprintf(value,sizeof(value),"%s cars=%d other=%d people=%d bpm=%.0f amplitude=%.3f easing=%d",action,cfg.enabled,cfg.otherVehicles,cfg.allPeople,cfg.bpm,cfg.amplitude,cfg.easing);
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
#ifdef VICEBOUNCE_DIAGNOSTICS
void DiagHeartbeat();
#endif
void Tick() {
    // Runs at the 3D/2D boundary, after deferred transparent vehicle atomics.
    DIAG_STAGE("Tick:restore");
    RestoreFrames();
#ifdef VICEBOUNCE_DIAGNOSTICS
    DiagHeartbeat();
#endif
    DIAG_STAGE("Tick");
    static ULONGLONG previous=GetTickCount64();
    static std::array<bool,10> wasDown{};
    constexpr int keys[]={VK_F6,VK_F7,VK_PRIOR,VK_NEXT,VK_HOME,VK_END,VK_F8,VK_F9,VK_F10,VK_F11};
    const auto now=GetTickCount64();
    const double elapsed=(now-previous)/1000.0;previous=now;
    const bool active=Focused()&&!Paused();
    // Open audio lazily on the game thread, never from ASI/DllMain initialization.
    const int importResult=importer.Poll();
    if(importResult==0) {LoadConfig(false);Log("local music import/check completed; reloading track profile");}
    else if(importResult>0&&importResult!=2) Log("music import failed; see ViceBounceImportStatus.json; previous profile retained");
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
    std::array<bool,10> pressed{};
    for(size_t i=0;i<wasDown.size();++i) {
        const bool down=(GetAsyncKeyState(keys[i])&0x8000)!=0;
        pressed[i]=active&&down&&!wasDown[i];wasDown[i]=down;
    }
    if(!active) return;
    if(!synced&&(cfg.enabled||cfg.otherVehicles||cfg.allPeople)&&elapsed<=0.25) phase=AdvancePhase(phase,elapsed,cfg.bpm);
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
    // Ctrl+Shift+F11 belongs to OBS. Only bare F11 opens the music picker.
    if(pressed[9]&&!(GetAsyncKeyState(VK_CONTROL)&0x8000)&&!(GetAsyncKeyState(VK_SHIFT)&0x8000)&&!(GetAsyncKeyState(VK_MENU)&0x8000)) {
        if(importer.Start(musicDirectory,true)) Log("music file picker opened");
        else Log("music picker unavailable or import already running");
    }
}

struct SavedFrame { RwFrame* frame=nullptr; RwMatrix original{}; };
std::vector<SavedFrame> savedFrames;
struct SavedBones { RwMatrix* matrices; std::vector<RwMatrix> originals; };
std::vector<SavedBones> savedBones;
struct VehicleGroundOffset { CVehicle* vehicle; float z; };
std::vector<VehicleGroundOffset> groundOffsets;
#ifdef VICEBOUNCE_DIAGNOSTICS
void DiagHeartbeat() {
    static bool first=true;
    if(first) {first=false;diag::ListModules();diag::StartWatchdog();}
    ++diag::frames;
    static bool wasCutscene=false,known=false;
    const bool cutscene=CutscenePlaying();
    if(!known||cutscene!=wasCutscene) {
        Log(std::string("diag: cutscene flag ")+(cutscene?"set":"clear")+" at frame "+std::to_string(diag::frames));
        wasCutscene=cutscene;known=true;
    }
    static ULONGLONG nextBeat=0;
    static int beats=0;
    const auto now=GetTickCount64();
    if(now>=nextBeat&&beats<200) {
        nextBeat=now+2000;++beats;
        Log("diag: heartbeat frame="+std::to_string(diag::frames)+" cutscene="+std::to_string(int(cutscene))+
            " codePause="+std::to_string(int(CTimer::m_CodePause))+" userPause="+std::to_string(int(CTimer::m_UserPause))+
            " replay="+std::to_string(int(ReplayPlaying()))+" savedFrames="+std::to_string(savedFrames.size())+
            " savedBones="+std::to_string(savedBones.size())+" importerRunning="+std::to_string(int(importer.Running()))+
            " stage="+diag::stage);
    }
}
void DiagCutsceneDelete(const char* what,CCutsceneObject* actor) {
    static int logged=0;
    if(logged++>=120) return;
    Log(std::string("diag: ")+what+" model="+std::to_string(actor->m_nModelIndex)+" savedFrames="+std::to_string(savedFrames.size())+
        " savedBones="+std::to_string(savedBones.size())+" frame="+std::to_string(diag::frames));
}
#endif

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
        // The game's RW geometry omits the SDK header's obsolete surface-properties field (Steam and 1.0 alike:
        // both executables read the vertex count at +0x14 and the morph target at +0x5C, see GameLayout.h).
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
// The game's HAnim layout (Steam and 1.0 alike) differs from the legacy SDK header.
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
    // Array/count verified in StartCutscene for each executable (see GameLayout.h).
    const int count=*reinterpret_cast<const int*>(plugin::GetGlobalAddress(layout->cutsceneCount));
    if(count<=0||count>50) return nullptr;
    auto** actors=reinterpret_cast<CCutsceneObject**>(plugin::GetGlobalAddress(layout->cutsceneActors));
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
    DIAG_STAGE("BeforePedRender");
    if(!ped||cfg.amplitude<=0||Paused()) return;
    auto* vehicle=PedBounceVehicle(ped);
    const int effect=PedEffect(cfg.allPeople,EffectActive(vehicle),ped->m_bInVehicle);
    if(!effect) return;
    // CPed::Render copies the complete hand matrix to the weapon frame after
    // this hook (Steam 0x4FE11D..0x4FE17F, 1.0 0x4FE25D..0x4FE2BF). Do not scale the weapon a second time.
    if(effect==2) DeformActor(ped,vehicle->GetMatrix(),GroundOffset(vehicle));
    else DeformStandingActor(ped);
    static bool logged=false;
    if(!logged){Log("first person rendered; skeleton and held weapon hand matrix deformed");logged=true;}
    static bool doorLogged=false;
    if(!ped->m_bInVehicle&&EffectActive(vehicle)&&!doorLogged){Log("door contact/entry actor rendered before in-vehicle flag");doorLogged=true;}
}
void __fastcall RenderCutscene(CCutsceneObject* actor,void*) {
    DIAG_STAGE("RenderCutscene:deform");
    if((cfg.enabled||cfg.allPeople)&&cfg.amplitude>0&&!Paused()) {
        DeformCutsceneActor(actor);
        static bool logged=false;
        if(!logged){Log("cutscene actors and attached props rendered with bounce");logged=true;}
    }
    DIAG_STAGE("RenderCutscene:game");
    originalCutsceneRender(actor);
    DIAG_STAGE("RenderCutscene:done");
}
void RestoreFrames() {
    DIAG_STAGE("RestoreFrames");
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
    DIAG_STAGE("DeleteCutscene:restore");
#ifdef VICEBOUNCE_DIAGNOSTICS
    DiagCutsceneDelete("cutscene object destructor",actor);
#endif
    RestoreFrames(); // Drop all borrowed RW/bone pointers while owners are still alive.
    DIAG_STAGE("DeleteCutscene:game");
    auto* result=originalCutsceneDelete(actor,flags);
    DIAG_STAGE("DeleteCutscene:done");
    return result;
}
void __fastcall DeleteCutsceneRw(CCutsceneObject* actor,void*) {
    DIAG_STAGE("DeleteCutsceneRw:restore");
#ifdef VICEBOUNCE_DIAGNOSTICS
    DiagCutsceneDelete("cutscene DeleteRwObject",actor);
#endif
    RestoreFrames();
    DIAG_STAGE("DeleteCutsceneRw:game");
    originalCutsceneDeleteRw(actor);
    DIAG_STAGE("DeleteCutsceneRw:done");
}
void __fastcall RenderTransport(CVehicle* vehicle,void*) {
    DIAG_STAGE("RenderTransport:deform");
    BeforeRender(vehicle);
    auto* previous=lightingVehicle;
    lightingVehicle=EffectActive(vehicle)?vehicle:nullptr;
    DIAG_STAGE("RenderTransport:game");
    originalRenders[vehicle->m_nVehicleClass](vehicle);
    DIAG_STAGE("RenderTransport:done");
    lightingVehicle=previous;
}
void __fastcall PreRenderTransport(CVehicle* vehicle,void*) {
    DIAG_STAGE("PreRenderTransport");
    auto* previous=lightingVehicle;
    lightingVehicle=EffectActive(vehicle)?vehicle:nullptr;
    originalPreRenders[vehicle->m_nVehicleClass](vehicle);
    DIAG_STAGE("PreRenderTransport:done");
    lightingVehicle=previous;
}
template<class Function> Function GameFunction(uintptr_t address) {
    return reinterpret_cast<Function>(plugin::GetGlobalAddress(address));
}
CVector LightPosition(CVector position) {
    if(lightingVehicle) {
        position=DeformPosition(position,lightingVehicle->GetMatrix(),ActiveScale());
        position.z+=GroundOffset(lightingVehicle);
    }
    return position;
}
CVector LightDirection(CVector direction) {
    return lightingVehicle?DeformDirection(direction,lightingVehicle->GetMatrix(),ActiveScale()):direction;
}
float LightRadius(float radius) { return lightingVehicle?radius*ActiveScale().horizontal:radius; }
void __cdecl RegisterCorona(unsigned id,unsigned char r,unsigned char g,unsigned char b,unsigned char alpha,
    const CVector& position,float radius,float farClip,unsigned char type,unsigned char flare,unsigned char reflection,
    unsigned char check,unsigned char streak,float angle,bool longDistance,float nearClip) {
    using Fn=decltype(&RegisterCorona);
    DIAG_STAGE("light:RegisterCorona");
    const auto transformed=LightPosition(position);
    GameFunction<Fn>(layout->registerCorona)(id,r,g,b,alpha,transformed,LightRadius(radius),farClip,type,flare,reflection,check,streak,angle,longDistance,nearClip);
    static bool logged=false;if(lightingVehicle&&!logged){Log("first automobile light registered; corona positions and size deformed");logged=true;}
}
void __cdecl UpdateCorona(unsigned id,const CVector& position,float farClip,float angle) {
    DIAG_STAGE("light:UpdateCorona");
    const auto transformed=LightPosition(position);
    GameFunction<decltype(&UpdateCorona)>(layout->updateCorona)(id,transformed,farClip,angle);
}
void __cdecl AddPointLight(unsigned char type,CVector position,CVector direction,float range,float r,float g,float b,unsigned char fog,bool shadows) {
    DIAG_STAGE("light:AddPointLight");
    GameFunction<decltype(&AddPointLight)>(layout->addPointLight)(type,LightPosition(position),LightDirection(direction),LightRadius(range),r,g,b,fog,shadows);
}
void __cdecl RegisterBrightLight(CVector position,CVector right,CVector up,CVector at,unsigned char r,unsigned char g,unsigned char b,unsigned char alpha) {
    DIAG_STAGE("light:RegisterBrightLight");
    GameFunction<decltype(&RegisterBrightLight)>(layout->registerBrightLight)(LightPosition(position),LightDirection(right),LightDirection(up),LightDirection(at),r,g,b,alpha);
}
void __cdecl StoreLightShadow(CVehicle* vehicle,int id,RwTexture* texture,CVector* position,float frontX,float frontY,float sideX,float sideY,
    unsigned char r,unsigned char g,unsigned char b,float maxAngle) {
    DIAG_STAGE("light:StoreLightShadow");
    auto transformed=LightPosition(*position);
    transformed.z=position->z; // The footprint remains projected onto the road.
    const auto front=LightDirection(CVector(frontX,frontY,0));
    const auto side=LightDirection(CVector(sideX,sideY,0));
    GameFunction<decltype(&StoreLightShadow)>(layout->storeLightShadow)(vehicle,id,texture,&transformed,front.x,front.y,side.x,side.y,r,g,b,maxAngle);
    static bool logged=false;if(lightingVehicle&&!logged){Log("first automobile projected light registered; road footprint deformed");logged=true;}
}
uintptr_t LightReplacement(uintptr_t target) {
    if(target==layout->registerCorona) return reinterpret_cast<uintptr_t>(&RegisterCorona);
    if(target==layout->updateCorona) return reinterpret_cast<uintptr_t>(&UpdateCorona);
    if(target==layout->addPointLight) return reinterpret_cast<uintptr_t>(&AddPointLight);
    if(target==layout->registerBrightLight) return reinterpret_cast<uintptr_t>(&RegisterBrightLight);
    if(target==layout->storeLightShadow) return reinterpret_cast<uintptr_t>(&StoreLightShadow);
    return 0;
}
// The live process as the Memory source for VerifyLayout (GameLayoutCheck.h).
struct ProcessMemory {
    const unsigned char* At(uintptr_t address) const { return reinterpret_cast<const unsigned char*>(plugin::GetGlobalAddress(address)); }
    uintptr_t Abs(uintptr_t address) const { return plugin::GetGlobalAddress(address); }
};
// Picks the table for the running executable and verifies every address in it against the game's
// own bytes (GameLayoutCheck.h). Only a fully confirmed table is ever used to patch the game.
bool CheckGame() {
    const unsigned gameVersion=plugin::GetGameVersion();
    const GameLayout* candidate=nullptr;
    if(gameVersion==GAME_10EN) candidate=&layout10EN;
    else if(gameVersion==GAME_STEAM) candidate=&layoutSteam;
    if(!candidate) {
        Log(std::string("unsupported executable (")+plugin::GetGameVersionName()+"): this build supports 1.0 EN and Steam");
        return false;
    }
    std::string failure;
    if(!VerifyLayout(*candidate,ProcessMemory{},failure)) {
        Log("executable verification failed: "+failure+"; hooks disabled");return false;
    }
    layout=candidate;
    transportHooks={{
        {VEHICLE_AUTOMOBILE,layout->automobile.vtable,layout->automobile.preRender,layout->automobile.render},
        {VEHICLE_BOAT,layout->boat.vtable,layout->boat.preRender,layout->boat.render},
        {VEHICLE_HELI,layout->heli.vtable,layout->heli.preRender,layout->heli.render},
        {VEHICLE_PLANE,layout->plane.vtable,layout->plane.preRender,layout->plane.render},
        {VEHICLE_BIKE,layout->bike.vtable,layout->bike.preRender,layout->bike.render}
    }};
    updateFrame=GameFunction<UpdateFrameFn>(layout->updateFrame);
    getLtm=GameFunction<GetLtmFn>(layout->getLtm);
    getHierarchy=GameFunction<GetHierarchyFn>(layout->getHierarchy);
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
        Log(std::string("ViceBounce 0.11.1 startup (diagnostic build); ")+plugin::GetGameVersionName());
#else
        Log(std::string("ViceBounce 0.11.1 startup; ")+plugin::GetGameVersionName());
#endif
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
            writable.push_back({reinterpret_cast<void*>(plugin::GetGlobalAddress(transport.vtable+12*sizeof(RenderFn))),2*sizeof(RenderFn),0});
        writable.push_back({reinterpret_cast<void*>(plugin::GetGlobalAddress(layout->cutsceneVtable+2*sizeof(uintptr_t))),12*sizeof(uintptr_t),0});
        for(const auto& site:layout->lightCallsites) writable.push_back({reinterpret_cast<void*>(plugin::GetGlobalAddress(site.address)),5,0});
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
            auto* slots=reinterpret_cast<RenderFn*>(plugin::GetGlobalAddress(transport.vtable));
            originalRenders[transport.runtimeType]=slots[13];
            originalPreRenders[transport.runtimeType]=slots[12];
            slots[13]=reinterpret_cast<RenderFn>(&RenderTransport);
            slots[12]=reinterpret_cast<RenderFn>(&PreRenderTransport);
        }
        auto* deleteSlot=reinterpret_cast<CutsceneDeleteFn*>(plugin::GetGlobalAddress(layout->cutsceneVtable+2*sizeof(uintptr_t)));
        originalCutsceneDelete=*deleteSlot;*deleteSlot=reinterpret_cast<CutsceneDeleteFn>(&DeleteCutscene);
        auto* deleteRwSlot=reinterpret_cast<CutsceneDeleteRwFn*>(plugin::GetGlobalAddress(layout->cutsceneVtable+6*sizeof(uintptr_t)));
        originalCutsceneDeleteRw=*deleteRwSlot;*deleteRwSlot=reinterpret_cast<CutsceneDeleteRwFn>(&DeleteCutsceneRw);
        auto* cutsceneSlot=reinterpret_cast<CutsceneRenderFn*>(plugin::GetGlobalAddress(layout->cutsceneVtable+13*sizeof(CutsceneRenderFn)));
        originalCutsceneRender=*cutsceneSlot;
        *cutsceneSlot=reinterpret_cast<CutsceneRenderFn>(&RenderCutscene);
        for(const auto& site:layout->lightCallsites) {
            auto* code=reinterpret_cast<unsigned char*>(plugin::GetGlobalAddress(site.address));
            const int32_t rel=static_cast<int32_t>(LightReplacement(site.target)-uintptr_t(code+5));
            std::memcpy(code+1,&rel,sizeof(rel));
        }
        for(auto i=writable.rbegin();i!=writable.rend();++i){DWORD ignored;VirtualProtect(i->address,i->size,i->protection,&ignored);}
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);
        plugin::Events::pedRenderEvent.before+=BeforePedRender;
        plugin::Events::drawingEvent.before+=Tick;
        plugin::Events::gameProcessEvent.before+=RestoreFrames;
        Log(std::string("verified ")+layout->name+" full Render/PreRender for cars, bikes, boats, helicopters and planes; occupants, "+std::to_string(layout->lightCallsites.size())+" light callsites, replay, cutscenes, atomic aircraft, standing actor anchors and cutscene destruction guards enabled");
    }
} instance;
}
