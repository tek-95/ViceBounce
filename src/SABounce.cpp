// SABounce: GTA San Andreas port of ViceBounce (https://github.com/tek-95/ViceBounce, MIT).
// Targets gta_sa.exe 1.0 US. Every address used here is checked against the executable at startup.
#include "plugin.h"
#include "CVehicle.h"
#include "CAutomobile.h"
#include "CBike.h"
#include "CPed.h"
#include "CModelInfo.h"
#include "CColModel.h"
#include "CTimer.h"
#include "CReplay.h"
#include "CCutsceneMgr.h"
#include "CCutsceneObject.h"
#include "CPedIntelligence.h"
#include "CTaskManager.h"
#include "CTask.h"
#include "CPools.h"
#include "CHeli.h"
#include "common.h"
#include "safetyhook.hpp"
#include "BounceMath.h"
#include "BeatMap.h"
#include "MusicPlayback.h"
#include "MusicImporter.h"
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace {
using namespace sabounce;
constexpr const char* version="0.3.6";
Settings cfg;
double phase=0;
std::string iniPath,logPath;
MusicPlayback music;
std::wstring musicFile,musicDirectory,beatMapFile;
BeatMap beatMap;
MusicImporter importer;
bool importRequested=false,useBeatMap=true;
double musicSyncOffsetMs=0,firstBeatMs=0;
bool musicEnabled=true,musicPending=false,musicErrorLogged=false,musicStartedLogged=false;
SafetyHookInline renderOneNonRoadHook;

// Verified 1.0 US addresses (see MODLOG.md).
constexpr uintptr_t renderOneNonRoad=0x553260;
constexpr uintptr_t entityRender=0x534310;
constexpr uintptr_t pedRenderCalls[]={0x5E77FC,0x5E780A};
constexpr uintptr_t drawingCall=0x53E293,drawingTarget=0x60F8C0;
constexpr uintptr_t registerCoronaAddress=0x6FC180,addPointLightAddress=0x7000E0,storeCarLightShadowAddress=0x70C500;
constexpr uintptr_t addHeliSearchLightAddress=0x6C45B0,headLightBeamAddress=0x6E0E20;

void Log(const std::string& text) {
    std::ofstream out(logPath,std::ios::app);
    out << GetTickCount64() << " " << text << '\n';
}
void Status(const char* action) {
    char value[160];
    std::snprintf(value,sizeof(value),"%s cars=%d other=%d people=%d bpm=%.0f amplitude=%.3f easing=%d",action,cfg.enabled,cfg.otherVehicles,cfg.allPeople,cfg.bpm,cfg.amplitude,cfg.easing);
    Log(value);
}

int TransportKind(CVehicle* vehicle) { return vehicle->m_nVehicleSubClass; }
bool ReplayPlaying() { return CReplay::Mode==1; }
bool CutscenePlaying() { return CCutsceneMgr::ms_running; }
bool Paused() { return VisualPaused(CTimer::m_UserPause,CTimer::m_CodePause,ReplayPlaying(),CutscenePlaying()); }
bool EffectActive(CVehicle* vehicle) { return vehicle&&VehicleEnabled(cfg,TransportKind(vehicle))&&cfg.amplitude>0&&!Paused(); }
Scale ActiveScale() { auto visual=cfg;visual.enabled=true;return GetScale(phase,visual); }
void RestoreFrames();
void InstallLightHooks();

std::wstring BesideIni(const std::wstring& folder,const wchar_t* path) {
    std::wstring value(path);
    if(!value.empty()&&value.find(L':')==std::wstring::npos&&value[0]!=L'\\'&&value[0]!=L'/') value=folder+value;
    return value;
}
void LoadConfig(bool requestImport=true) {
    char value[96];
    auto read=[&](const char* key,const char* fallback){GetPrivateProfileStringA("SABounce",key,fallback,value,sizeof(value),iniPath.c_str());return value;};
    cfg.bpm=ParseNumber(read("BPM","135"),135,20,300);
    cfg.amplitude=ParseNumber(read("Amplitude","0.20"),0.20,0,0.75);
    cfg.easing=int(ParseNumber(read("Easing","1"),1,0,3));
    cfg.enabled=ParseNumber(read("Enabled","1"),1,0,1)>=0.5;
    cfg.otherVehicles=ParseNumber(read("OtherVehicles","1"),1,0,1)>=0.5;
    cfg.allPeople=ParseNumber(read("AllPeople","0"),0,0,1)>=0.5;
    musicEnabled=ParseNumber(read("MusicEnabled","1"),1,0,1)>=0.5;
    firstBeatMs=ParseNumber(read("MusicFirstBeatMs","0"),0,-3600000,3600000);
    useBeatMap=ParseNumber(read("MusicUseBeatMap","1"),1,0,1)>=.5;
    musicSyncOffsetMs=ParseNumber(read("MusicSyncOffsetMs","0"),0,-3600000,3600000);
    const bool autoImport=ParseNumber(read("MusicAutoImport","1"),1,0,1)>=.5;
    wchar_t module[MAX_PATH]{},path[1024]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),module,MAX_PATH);
    std::wstring folder(module);folder=folder.substr(0,folder.find_last_of(L"\\/")+1);
    musicDirectory=folder;
    const auto wideIni=folder+L"SABounce.ini";
    GetPrivateProfileStringW(L"SABounce",L"MusicFile",L"SABounceMusic.mp3",path,1024,wideIni.c_str());
    musicFile=BesideIni(folder,path);
    GetPrivateProfileStringW(L"SABounce",L"MusicBeatMapFile",L"",path,1024,wideIni.c_str());
    beatMapFile=BesideIni(folder,path);
    GetPrivateProfileStringW(L"SABounce",L"MusicSourceFile",L"",path,1024,wideIni.c_str());
    const auto source=BesideIni(folder,path);
    importer.Configure(source,autoImport);
    importRequested=requestImport&&musicEnabled&&autoImport&&!source.empty();
    musicPending=true;musicErrorLogged=false;musicStartedLogged=false;
    Status("config loaded");
}
void SaveValue(const char* name,double value) {
    char text[40];std::snprintf(text,sizeof(text),"%.4f",value);
    WritePrivateProfileStringA("SABounce",name,text,iniPath.c_str());
}
bool Focused() {
    DWORD process=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&process);
    return process==GetCurrentProcessId();
}
void Tick() {
    // Runs inside Render2dStuff, after RenderScene, weapons and effects: all 3D for this frame is done.
    RestoreFrames();
    InstallLightHooks();
    static ULONGLONG previous=GetTickCount64();
    static std::array<bool,10> wasDown{};
    constexpr int keys[]={VK_F6,VK_F7,VK_PRIOR,VK_NEXT,VK_HOME,VK_END,VK_F8,VK_F9,VK_F10,VK_F11};
    const auto now=GetTickCount64();
    const double elapsed=(now-previous)/1000.0;previous=now;
    const bool active=Focused()&&!Paused();
    // Audio opens lazily on the game thread, never from DllMain.
    const int importResult=importer.Poll();
    if(importResult==0) {LoadConfig(false);Log("local music import/check completed; reloading track profile");}
    else if(importResult>0&&importResult!=2) Log("music import failed; see SABounceImportStatus.json; previous profile retained");
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

RwFrame* RootFrame(CEntity* entity) {
    if(!entity->m_pRwObject||!RenderableObjectType(entity->m_pRwObject->type)) return nullptr;
    return static_cast<RwFrame*>(entity->m_pRwObject->parent);
}
bool AlreadySaved(RwFrame* frame) {
    for(const auto& saved:savedFrames) if(saved.frame==frame) return true;
    return false;
}
RpHAnimHierarchy* WorldSkeleton(CEntity* entity) {
    if(!entity->m_pRwObject||entity->m_pRwObject->type!=rpCLUMP) return nullptr;
    auto* hierarchy=GetAnimHierarchyFromSkinClump(entity->m_pRwClump);
    // Local-space hierarchies would need the frame instead; SA peds use world-space bone matrices.
    if(!hierarchy||!hierarchy->pMatrixArray||hierarchy->numNodes<=0||hierarchy->numNodes>128||(hierarchy->flags&0x4000)) return nullptr;
    return hierarchy;
}
template<class Visit> void ForEachVertex(RwFrame* frame,Visit visit,int depth=0) {
    if(!frame||depth>=32) return;
    const auto* world=RwFrameGetLTM(frame);
    auto* sentinel=&frame->objectList.link;
    int objects=0;
    for(auto* link=sentinel->next;link!=sentinel&&objects++<64;link=link->next) {
        auto* object=reinterpret_cast<RwObjectHasFrame*>(reinterpret_cast<char*>(link)-offsetof(RwObjectHasFrame,lFrame));
        if(object->object.type!=rpATOMIC||!(object->object.flags&rpATOMICRENDER)) continue;
        auto* geometry=reinterpret_cast<RpAtomic*>(object)->geometry;
        if(!geometry||geometry->numVertices<=0||geometry->numVertices>32768||geometry->numMorphTargets<=0) continue;
        const auto* morph=geometry->morphTarget;
        if(!morph||!morph->verts) continue;
        for(int i=0;i<geometry->numVertices;++i) {
            const auto& p=morph->verts[i];
            visit(RwV3d{world->pos.x+world->right.x*p.x+world->up.x*p.y+world->at.x*p.z,
                        world->pos.y+world->right.y*p.x+world->up.y*p.y+world->at.y*p.z,
                        world->pos.z+world->right.z*p.x+world->up.z*p.y+world->at.z*p.z});
        }
    }
    for(auto* child=frame->child;child;child=child->next) ForEachVertex(child,visit,depth+1);
}
float GroundOffset(CVehicle* vehicle) {
    for(const auto& saved:groundOffsets) if(saved.vehicle==vehicle) return saved.z;
    GroundAnchor anchor;
    const auto& basis=*vehicle->m_matrix;
    const auto scale=ActiveScale();
    const int kind=TransportKind(vehicle);
    if(!GroundedKind(kind)) {groundOffsets.push_back({vehicle,0});return 0;}
    auto addPoint=[&](RwV3d p){anchor.Add(p.z,DeformPosition(p,basis,scale).z);};
    if(vehicle->m_nVehicleClass==VEHICLE_AUTOMOBILE) {
        auto* automobile=static_cast<CAutomobile*>(vehicle);
        for(int node:{CAR_WHEEL_RF,CAR_WHEEL_RM,CAR_WHEEL_RB,CAR_WHEEL_LF,CAR_WHEEL_LM,CAR_WHEEL_LB})
            ForEachVertex(automobile->m_aCarNodes[node],addPoint);
    } else if(vehicle->m_nVehicleClass==VEHICLE_BIKE) {
        auto* bike=static_cast<CBike*>(vehicle);
        for(int node:{BIKE_WHEEL_FRONT,BIKE_WHEEL_REAR}) ForEachVertex(bike->m_aBikeNodes[node],addPoint);
    }
    const bool wheelGeometry=anchor.Valid();
    if(!anchor.Valid()) {
        auto* model=CModelInfo::GetModelInfo(vehicle->m_nModelIndex);
        if(model&&model->m_pColModel) {
            const auto& box=model->m_pColModel->m_boundBox;
            for(float x:{box.m_vecMin.x,box.m_vecMax.x})
                for(float y:{box.m_vecMin.y,box.m_vecMax.y})
                    for(float z:{box.m_vecMin.z,box.m_vecMax.z})
                        addPoint({basis.pos.x+basis.right.x*x+basis.up.x*y+basis.at.x*z,
                                  basis.pos.y+basis.right.y*x+basis.up.y*y+basis.at.y*z,
                                  basis.pos.z+basis.right.z*x+basis.up.z*y+basis.at.z*z});
        }
    }
    const float offset=anchor.Offset();
    groundOffsets.push_back({vehicle,offset});
    static bool logged[2]{};
    if(anchor.Valid()&&!logged[wheelGeometry]){Log(wheelGeometry?"wheel mesh ground anchor active; tyre lower extent preserved":"ground anchor active using collision bounds fallback");logged[wheelGeometry]=true;}
    return offset;
}
unsigned long rendered=0;
bool mismatchLogged=false;
void BeforeVehicleRender(CVehicle* vehicle) {
    if(!EffectActive(vehicle)||!vehicle->m_matrix) return;
    auto* frame=RootFrame(vehicle);
    if(!frame||AlreadySaved(frame)) return;
    const float groundOffset=GroundOffset(vehicle);
    savedFrames.push_back({frame,*RwFrameGetMatrix(frame)});
    *RwFrameGetMatrix(frame)=ScaledMatrix(savedFrames.back().original,ActiveScale());
    RwFrameGetMatrix(frame)->pos.z+=groundOffset;
    RwFrameUpdateObjects(frame);RwFrameGetLTM(frame);
    if(rendered++==0) Log("first vehicle rendered with bounce");
    static bool replayLogged=false;
    if(ReplayPlaying()&&!replayLogged){Log("replay vehicle rendered with bounce");replayLogged=true;}
    static std::array<bool,12> loggedTypes{};
    const int kind=TransportKind(vehicle);
    if(kind>=0&&kind<int(loggedTypes.size())&&!loggedTypes[kind]) {
        Log("transport kind "+std::to_string(kind)+" model "+std::to_string(vehicle->m_nModelIndex)+" rendered with bounce");loggedTypes[kind]=true;
    }
}
template<class Basis> void DeformActor(CEntity* entity,const Basis& basis,float groundOffset) {
    auto* frame=RootFrame(entity);
    if(!frame||AlreadySaved(frame)) return;
    const auto scale=ActiveScale();
    savedFrames.push_back({frame,*RwFrameGetMatrix(frame)});
    *RwFrameGetMatrix(frame)=DeformedMatrix(savedFrames.back().original,basis,scale);
    RwFrameGetMatrix(frame)->pos.z+=groundOffset;
    RwFrameUpdateObjects(frame);RwFrameGetLTM(frame);
    if(auto* hierarchy=WorldSkeleton(entity)) {
        for(const auto& saved:savedBones) if(saved.matrices==hierarchy->pMatrixArray) return;
        savedBones.push_back({hierarchy->pMatrixArray,{hierarchy->pMatrixArray,hierarchy->pMatrixArray+hierarchy->numNodes}});
        for(int i=0;i<hierarchy->numNodes;++i) {
            hierarchy->pMatrixArray[i]=DeformedMatrix(savedBones.back().originals[i],basis,scale);
            hierarchy->pMatrixArray[i].pos.z+=groundOffset;
        }
    }
}
float ActorGroundOffset(CEntity* actor,const CMatrix& basis) {
    auto* hierarchy=WorldSkeleton(actor);
    if(!hierarchy) return 0;
    GroundAnchor anchor;
    const auto scale=ActiveScale();
    for(int i=0;i<hierarchy->numNodes;++i) {
        const auto& p=hierarchy->pMatrixArray[i].pos;
        anchor.Add(p.z,DeformPosition(p,basis,scale).z);
    }
    return anchor.Offset();
}
bool IsVehiclePointer(uintptr_t value) {
    auto* pool=CPools::ms_pVehiclePool;
    if(!pool||!pool->m_pObjects||!value) return false;
    const auto start=reinterpret_cast<uintptr_t>(pool->m_pObjects);
    if(value<start) return false;
    const auto offset=value-start,size=uintptr_t(sizeof(CHeli));
    if(offset%size) return false;
    const auto slot=int(offset/size);
    return slot<pool->m_nSize&&!pool->IsFreeSlotAtIndex(slot);
}
// A ped opening a door, climbing in or out, shuffling or being dragged is not yet (or no longer) flagged as
// in the vehicle. Its simplest active task says which vehicle it is working with; the task layouts differ, so
// the first fields are matched against the live vehicle pool instead of hardcoding one offset per task class.
CVehicle* DoorInteractionVehicle(CPed* ped) {
    if(!ped->m_pIntelligence) return nullptr;
    auto* task=ped->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
    if(!task||!DoorInteractionTask(task->GetId())) return nullptr;
    const auto* fields=reinterpret_cast<const uintptr_t*>(task);
    for(int i=2;i<10;++i) if(IsVehiclePointer(fields[i])) return reinterpret_cast<CVehicle*>(fields[i]);
    return nullptr;
}
void BeforePedRender(CPed* ped) {
    if(!ped||!ped->m_matrix||cfg.amplitude<=0||Paused()) return;
    auto* vehicle=ped->bInVehicle?ped->m_pVehicle:nullptr;
    if(!vehicle&&(vehicle=DoorInteractionVehicle(ped))) {
        static bool logged=false;if(!logged&&EffectActive(vehicle)){Log("door entry/exit ped bounced with its vehicle");logged=true;}
    }
    const bool vehicleActive=EffectActive(vehicle)&&vehicle->m_matrix;
    const int effect=PedEffect(cfg.allPeople,vehicleActive,vehicle!=nullptr);
    if(!effect) return;
    static bool flagsLogged=false;
    if(!flagsLogged&&ped->m_pRwObject&&ped->m_pRwObject->type==rpCLUMP) {
        auto* hierarchy=GetAnimHierarchyFromSkinClump(ped->m_pRwClump);
        char text[96];std::snprintf(text,sizeof(text),"first ped skeleton: flags=0x%X nodes=%d",hierarchy?hierarchy->flags:-1,hierarchy?hierarchy->numNodes:-1);
        Log(text);flagsLogged=true;
    }
    // Weapons are attached later from the deformed hand bone (RenderWeaponPedsForPC), so they follow it.
    if(effect==2) DeformActor(ped,*vehicle->m_matrix,GroundOffset(vehicle));
    else DeformActor(ped,*ped->m_matrix,ActorGroundOffset(ped,*ped->m_matrix));
    static bool logged[3]{};
    if(!logged[effect]){Log(effect==2?"first occupant rendered with its vehicle's bounce":"first standing ped rendered with bounce");logged[effect]=true;}
}
// Vehicle lights. Every vehicle light is registered from its own PreRender (coronas, sirens, point lights, light
// shadows on the road, heli searchlight, sun glare) or Render (headlight beams), always from the undeformed CMatrix.
// While one of those runs for a bouncing vehicle it is the "lighting vehicle", and the light API entry points move
// what they receive onto the deformed body. Object-space coronas (attached to the vehicle) are deformed locally.
struct LightingScope {
    CVehicle* previous;float previousGround;
    explicit LightingScope(CVehicle* vehicle);
    ~LightingScope();
};
CVehicle* lightingVehicle=nullptr;
float lightingGround=0;
LightingScope::LightingScope(CVehicle* vehicle):previous(lightingVehicle),previousGround(lightingGround) {
    const bool active=EffectActive(vehicle)&&vehicle->m_matrix;
    lightingVehicle=active?vehicle:nullptr;
    lightingGround=active?GroundOffset(vehicle):0;
}
LightingScope::~LightingScope() { lightingVehicle=previous;lightingGround=previousGround; }
CVector WorldLight(const CVector& position) {
    auto moved=DeformPosition(position,*lightingVehicle->m_matrix,ActiveScale());
    moved.z+=lightingGround;
    return moved;
}
void LogOnce(bool& done,const char* text) { if(!done){Log(text);done=true;} }

SafetyHookMid registerCoronaHook,addPointLightHook,storeCarLightShadowHook,addHeliSearchLightHook;
SafetyHookInline headLightBeamHook;
void RegisterCoronaEntry(SafetyHookContext& context) {
    // cdecl: [esp+8] attachTo, [esp+0x1C] const CVector* position, [esp+0x20] float size.
    if(!lightingVehicle) return;
    auto* stack=reinterpret_cast<uintptr_t*>(context.esp);
    auto* attachTo=reinterpret_cast<CEntity*>(stack[2]);
    auto* position=reinterpret_cast<const CVector*>(stack[7]);
    if(!position||(attachTo&&attachTo!=lightingVehicle)) return;
    static CVector moved;
    const auto scale=ActiveScale();
    moved=attachTo?DeformLocal(*position,*lightingVehicle->m_matrix,scale,lightingGround):WorldLight(*position);
    stack[7]=reinterpret_cast<uintptr_t>(&moved);
    *reinterpret_cast<float*>(&stack[8])*=scale.horizontal;
    static bool logged[2]{};
    LogOnce(logged[attachTo!=nullptr],attachTo?"vehicle corona deformed in object space":"vehicle corona deformed in world space");
}
void AddPointLightEntry(SafetyHookContext& context) {
    // cdecl: [esp+8] CVector position and [esp+0x14] CVector direction by value, [esp+0x20] float radius.
    if(!lightingVehicle) return;
    auto* stack=reinterpret_cast<uintptr_t*>(context.esp);
    auto& position=*reinterpret_cast<CVector*>(&stack[2]);
    auto& direction=*reinterpret_cast<CVector*>(&stack[5]);
    const auto scale=ActiveScale();
    position=WorldLight(position);
    direction=DeformDirection(direction,*lightingVehicle->m_matrix,scale);
    *reinterpret_cast<float*>(&stack[8])*=scale.horizontal;
    static bool logged=false;LogOnce(logged,"vehicle point light deformed");
}
void StoreCarLightShadowEntry(SafetyHookContext& context) {
    // cdecl: [esp+0x10] CVector* position, [esp+0x14..0x20] front x/y, side x/y.
    if(!lightingVehicle) return;
    auto* stack=reinterpret_cast<uintptr_t*>(context.esp);
    auto* position=reinterpret_cast<CVector*>(stack[4]);
    if(!position) return;
    static CVector moved;
    moved=WorldLight(*position);
    moved.z=position->z; // the footprint stays projected onto the road
    stack[4]=reinterpret_cast<uintptr_t>(&moved);
    const auto scale=ActiveScale();
    auto* axes=reinterpret_cast<float*>(&stack[5]);
    for(int i=0;i<4;i+=2) {
        const auto axis=DeformDirection(CVector(axes[i],axes[i+1],0),*lightingVehicle->m_matrix,scale);
        axes[i]=axis.x;axes[i+1]=axis.y;
    }
    static bool logged=false;LogOnce(logged,"vehicle light shadow on the road deformed");
}
void AddHeliSearchLightEntry(SafetyHookContext& context) {
    // cdecl: [esp+4] const CVector* origin of the cone on the heli body; the target spot stays where it aims.
    if(!lightingVehicle) return;
    auto* stack=reinterpret_cast<uintptr_t*>(context.esp);
    auto* origin=reinterpret_cast<const CVector*>(stack[1]);
    if(!origin) return;
    static CVector moved;
    moved=WorldLight(*origin);
    stack[1]=reinterpret_cast<uintptr_t>(&moved);
    static bool logged=false;LogOnce(logged,"heli searchlight origin deformed");
}
// Headlight beam quads are built straight from the matrix argument (CAutomobile/CBike::Render, the bike passes
// its lean matrix), so the beam gets a deformed copy of that matrix.
void __fastcall DoHeadLightBeam(CVehicle* vehicle,void*,int light,CMatrix* matrix,bool right) {
    if(!matrix||lightingVehicle!=vehicle) {headLightBeamHook.thiscall<void>(vehicle,light,matrix,right);return;}
    // Raw copy: CMatrix's destructor may free an attached RwMatrix, so no CMatrix temporary is constructed.
    alignas(16) unsigned char storage[sizeof(CMatrix)];
    std::memcpy(storage,matrix,sizeof(CMatrix));
    auto* deformed=reinterpret_cast<CMatrix*>(storage);
    deformed->m_pAttachMatrix=nullptr;deformed->m_bOwnsAttachedMatrix=false;
    const auto scale=ActiveScale();
    deformed->right=DeformDirection(matrix->right,*vehicle->m_matrix,scale);
    deformed->up=DeformDirection(matrix->up,*vehicle->m_matrix,scale);
    deformed->at=DeformDirection(matrix->at,*vehicle->m_matrix,scale);
    deformed->pos=WorldLight(matrix->pos);
    auto* previous=lightingVehicle;lightingVehicle=nullptr; // the matrix is already deformed
    headLightBeamHook.thiscall<void>(vehicle,light,deformed,right);
    lightingVehicle=previous;
    static bool logged=false;LogOnce(logged,"headlight beam deformed");
}
// PreRender of every transport class, patched in its vtable. Originals are kept per vtable.
using RenderFn=void(__thiscall*)(CVehicle*);
struct PreRenderSlot { const char* name; uintptr_t vtable,expected; RenderFn original; };
PreRenderSlot preRenderSlots[]={
    {"CAutomobile",0x871120,0x6AAB50,nullptr},{"CBike",0x871360,0x6BD090,nullptr},{"CBmx",0x871528,0x6C0810,nullptr},
    {"CBoat",0x8721A0,0x6F1180,nullptr},{"CHeli",0x871680,0x6C5420,nullptr},{"CMonsterTruck",0x8717D8,0x6C7DE0,nullptr},
    {"CPlane",0x871948,0x6C94A0,nullptr},{"CQuadBike",0x871AE8,0x6CEAD0,nullptr},{"CTrailer",0x871C28,0x6CFAC0,nullptr},
    {"CTrain",0x872370,0x6F5570,nullptr}};
void __fastcall PreRenderTransport(CVehicle* vehicle,void*) {
    const auto vtable=*reinterpret_cast<const uintptr_t*>(vehicle);
    for(const auto& slot:preRenderSlots) if(slot.vtable==vtable&&slot.original) {
        LightingScope scope(vehicle);
        slot.original(vehicle);
        return;
    }
}

// Cutscenes. Actors and props are CCutsceneObjects that may all sit at the scene origin, so each gets an upright
// basis around its animated root bone (or its geometry centre). Humanoids keep their lowest bone on the ground;
// a prop held by a humanoid shares the holder's transform so it stays in the deformed hand.
struct ActorTransform { RwMatrix basis{};float offset=0;bool humanoid=false; };
struct CutsceneTransform { CCutsceneObject* actor;ActorTransform transform; };
std::vector<CutsceneTransform> cutsceneTransforms;
CCutsceneObject* CutsceneActor(CEntity* entity) {
    if(entity->m_nType!=ENTITY_TYPE_OBJECT||!CutscenePlaying()) return nullptr;
    const unsigned count=CCutsceneMgr::ms_numCutsceneObjs;
    if(count>50) return nullptr;
    for(unsigned i=0;i<count;++i) if(CCutsceneMgr::ms_pCutsceneObjects[i]==entity) return static_cast<CCutsceneObject*>(entity);
    return nullptr;
}
const RwMatrix* OriginalBones(RpHAnimHierarchy* hierarchy) {
    for(const auto& saved:savedBones) if(saved.matrices==hierarchy->pMatrixArray) return saved.originals.data();
    return hierarchy->pMatrixArray;
}
ActorTransform OwnCutsceneTransform(CCutsceneObject* actor) {
    for(const auto& cached:cutsceneTransforms) if(cached.actor==actor) return cached.transform;
    ActorTransform transform;
    auto* frame=RootFrame(actor);
    if(!frame) return transform;
    auto* hierarchy=WorldSkeleton(actor);
    RwV3d pivot=RwFrameGetLTM(frame)->pos;
    const RwMatrix* bones=hierarchy?OriginalBones(hierarchy):nullptr;
    if(bones) pivot=bones[0].pos;
    else {
        RwV3d low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
        ForEachVertex(frame,[&](RwV3d p){
            low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);});
        if(std::isfinite(low.x)) pivot={(low.x+high.x)*.5f,(low.y+high.y)*.5f,(low.z+high.z)*.5f};
    }
    transform.basis=UprightActorBasis(*RwFrameGetLTM(frame),pivot);
    transform.humanoid=bones&&hierarchy->numNodes>=15;
    if(transform.humanoid) {
        GroundAnchor anchor;
        for(int i=0;i<hierarchy->numNodes;++i) anchor.Add(bones[i].pos.z,DeformPosition(bones[i].pos,transform.basis,ActiveScale()).z);
        transform.offset=anchor.Offset();
    }
    cutsceneTransforms.push_back({actor,transform});
    return transform;
}
CCutsceneObject* CutsceneAttachmentOwner(CCutsceneObject* actor) {
    const unsigned count=CCutsceneMgr::ms_numCutsceneObjs;
    if(count>50) return nullptr;
    // m_nAttachBone (union with m_pAttachTo) is the bone index whenever m_pAttachmentObject is set.
    if(actor->m_pAttachmentObject) {
        for(unsigned i=0;i<count;++i) if(CCutsceneMgr::ms_pCutsceneObjects[i]==actor->m_pAttachmentObject) return CCutsceneMgr::ms_pCutsceneObjects[i];
        return nullptr;
    }
    if(!actor->m_pAttachTo) return nullptr;
    for(unsigned i=0;i<count;++i) {
        auto* owner=CCutsceneMgr::ms_pCutsceneObjects[i];
        if(!owner||owner==actor) continue;
        auto* root=RootFrame(owner);
        if(!root) continue;
        // Walk the real frame tree; never guess an owner by distance.
        auto* attached=actor->m_pAttachTo;
        for(int depth=0;attached&&depth<32;++depth) {
            if(attached==root) return owner;
            attached=static_cast<RwFrame*>(attached->object.parent);
        }
    }
    return nullptr;
}
void DeformCutsceneActor(CCutsceneObject* actor) {
    auto* frame=RootFrame(actor);
    if(!frame||AlreadySaved(frame)) return;
    auto transform=OwnCutsceneTransform(actor);
    bool held=false;
    if(!transform.humanoid) {
        if(auto* owner=CutsceneAttachmentOwner(actor)) {
            const auto parent=OwnCutsceneTransform(owner);
            if(parent.humanoid) {transform=parent;held=true;}
        }
    }
    // F6 drives props and cutscene cars; people bounce with F6 or F10.
    if(!cfg.enabled&&!transform.humanoid) return;
    DeformActor(actor,transform.basis,transform.offset);
    static std::vector<int> loggedModels;
    if(loggedModels.size()<64&&std::find(loggedModels.begin(),loggedModels.end(),actor->m_nModelIndex)==loggedModels.end()) {
        loggedModels.push_back(actor->m_nModelIndex);
        Log("cutscene actor model="+std::to_string(actor->m_nModelIndex)+" humanoid="+std::to_string(transform.humanoid)+" held="+std::to_string(held));
    }
}
void __cdecl RenderOneNonRoad(CEntity* entity) {
    if(entity&&entity->m_nType==ENTITY_TYPE_VEHICLE) {
        auto* vehicle=static_cast<CVehicle*>(entity);
        BeforeVehicleRender(vehicle);
        LightingScope scope(vehicle); // headlight beams are drawn from Render
        renderOneNonRoadHook.ccall<void>(entity);
        return;
    }
    if(entity&&(cfg.enabled||cfg.allPeople)&&cfg.amplitude>0&&!Paused())
        if(auto* actor=CutsceneActor(entity)) DeformCutsceneActor(actor);
    renderOneNonRoadHook.ccall<void>(entity);
}
void RestoreFrames() {
    for(const auto& saved:savedBones)
        std::memcpy(saved.matrices,saved.originals.data(),saved.originals.size()*sizeof(RwMatrix));
    savedBones.clear();
    for(const auto& saved:savedFrames) {
        auto* matrix=RwFrameGetMatrix(saved.frame);
        *matrix=saved.original;
        RwFrameUpdateObjects(saved.frame);RwFrameGetLTM(saved.frame);
        if(std::memcmp(matrix,&saved.original,sizeof(RwMatrix))!=0&&!mismatchLogged) {
            Log("ERROR: root matrix changed during restoration");mismatchLogged=true;
        }
    }
    savedFrames.clear();
    groundOffsets.clear();
    cutsceneTransforms.clear();
}

bool CallMatches(uintptr_t address,uintptr_t target) {
    auto* code=reinterpret_cast<const unsigned char*>(address);
    int32_t rel=0;std::memcpy(&rel,code+1,sizeof(rel));
    return code[0]==0xE8&&uintptr_t(code+5)+rel==target;
}
bool CheckExecutable() {
    const int game=plugin::GetGameVersion();
    if(game!=GAME_10US_COMPACT&&game!=GAME_10US_HOODLUM) {Log("unsupported executable: SABounce needs gta_sa.exe 1.0 US");return false;}
    constexpr unsigned char prologue[]={0x56,0x8B,0x74,0x24,0x08,0x8A,0x46,0x36,0x24,0x07,0x3C,0x03};
    if(std::memcmp(reinterpret_cast<const void*>(renderOneNonRoad),prologue,sizeof(prologue))!=0) {
        Log("CRenderer::RenderOneNonRoad differs from 1.0 US (another mod hooks it?); hooks disabled");return false;
    }
    for(auto site:pedRenderCalls) if(!CallMatches(site,entityRender)) {Log("CPed::Render callsite differs; hooks disabled");return false;}
    if(!CallMatches(drawingCall,drawingTarget)) {Log("Render2dStuff callsite differs; hooks disabled");return false;}
    return true;
}
// Optional: a mismatch here only leaves lights undeformed. Installed on the first rendered frame, after every
// other plugin has loaded: III.VC.SA.LimitAdjuster (Project2DFX) jumps RegisterCorona to its own copy during its
// init and would silently overwrite an earlier hook. A foreign jmp at the entry is accepted and chained.
bool EntryMatches(uintptr_t address,const unsigned char* expected,size_t size,const char* name) {
    const auto* code=reinterpret_cast<const unsigned char*>(address);
    if(std::memcmp(code,expected,size)==0) return true;
    if(code[0]==0xE9) {
        int32_t rel=0;std::memcpy(&rel,code+1,sizeof(rel));
        char text[128];std::snprintf(text,sizeof(text),"%s already redirected by another mod to %08X; chaining",name,unsigned(address+5+rel));
        Log(text);return true;
    }
    Log(std::string(name)+" differs from 1.0 US; lights stay undeformed");return false;
}
template<class Hook,class Make> void InstallEntry(Hook& hook,uintptr_t address,const unsigned char* expected,size_t size,const char* name,Make make) {
    if(!EntryMatches(address,expected,size,name)) return;
    hook=make(reinterpret_cast<void*>(address));
    if(!hook) Log(std::string("cannot hook ")+name+"; that light stays undeformed");
}
void InstallLightHooks() {
    static bool done=false;
    if(done) return;
    done=true;
    constexpr unsigned char corona[]={0x83,0xEC,0x1C,0x55,0x56,0x8B,0x74,0x24,0x2C};
    constexpr unsigned char cameraPrologue[]={0x8B,0x0D,0x3C,0xF0,0xB6,0x00}; // AddLight and StoreCarLightShadow
    constexpr unsigned char searchLight[]={0x8B,0x15,0x6C,0xC9,0xC1,0x00,0x8B,0x4C,0x24,0x04};
    constexpr unsigned char beam[]={0x81,0xEC,0x94,0x00,0x00,0x00,0x57,0x8B,0xF9};
    auto mid=[](safetyhook::MidHookFn callback){return [callback](void* target){return safetyhook::create_mid(target,callback);};};
    InstallEntry(registerCoronaHook,registerCoronaAddress,corona,sizeof(corona),"CCoronas::RegisterCorona",mid(&RegisterCoronaEntry));
    InstallEntry(addPointLightHook,addPointLightAddress,cameraPrologue,sizeof(cameraPrologue),"CPointLights::AddLight",mid(&AddPointLightEntry));
    InstallEntry(storeCarLightShadowHook,storeCarLightShadowAddress,cameraPrologue,sizeof(cameraPrologue),"CShadows::StoreCarLightShadow",mid(&StoreCarLightShadowEntry));
    InstallEntry(addHeliSearchLightHook,addHeliSearchLightAddress,searchLight,sizeof(searchLight),"CHeli::AddHeliSearchLight",mid(&AddHeliSearchLightEntry));
    InstallEntry(headLightBeamHook,headLightBeamAddress,beam,sizeof(beam),"CVehicle::DoHeadLightBeam",
        [](void* target){return safetyhook::create_inline(target,reinterpret_cast<void*>(&DoHeadLightBeam));});
    int patched=0;
    for(auto& slot:preRenderSlots) {
        auto* entry=reinterpret_cast<RenderFn*>(slot.vtable+17*sizeof(RenderFn));
        const auto current=reinterpret_cast<uintptr_t>(*entry);
        if(current!=slot.expected) Log(std::string(slot.name)+"::PreRender already replaced by another mod; chaining");
        DWORD protection=0;
        if(!VirtualProtect(entry,sizeof(RenderFn),PAGE_EXECUTE_READWRITE,&protection)) continue;
        slot.original=*entry;*entry=reinterpret_cast<RenderFn>(&PreRenderTransport);
        VirtualProtect(entry,sizeof(RenderFn),protection,&protection);
        ++patched;
    }
    Log("vehicle light hooks installed; PreRender patched for "+std::to_string(patched)+" transport classes");
}
struct SABounce {
    SABounce() {
        char module[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase),module,MAX_PATH);
        std::string dir(module);dir=dir.substr(0,dir.find_last_of("\\/")+1);
        iniPath=dir+"SABounce.ini";logPath=dir+"SABounce.log";
        Log(std::string("SABounce ")+version+" startup; "+plugin::GetGameVersionName());
        if(!CheckExecutable()) return;
        LoadConfig();
        savedFrames.reserve(256);savedBones.reserve(128);groundOffsets.reserve(256);cutsceneTransforms.reserve(50);
        renderOneNonRoadHook=safetyhook::create_inline(reinterpret_cast<void*>(renderOneNonRoad),reinterpret_cast<void*>(&RenderOneNonRoad));
        if(!renderOneNonRoadHook) {Log("cannot hook RenderOneNonRoad; hooks disabled");return;}
        plugin::Events::pedRenderEvent.before+=BeforePedRender;
        plugin::Events::drawingEvent.before+=Tick;
        plugin::Events::gameProcessEvent.before+=RestoreFrames;
        Log("hooks installed: vehicles and cutscene actors via RenderOneNonRoad, peds via CPed::Render, restore in Render2dStuff");
    }
} instance;
}
