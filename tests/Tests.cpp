#include "../src/BounceMath.h"
#include "../src/BeatMap.h"
#include "../src/Vegetation.h"
#include "../src/Buildings.h"
#include "../src/SceneryGroups.h"
#include <sstream>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace vicebounce;
void Require(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
bool Near(double a,double b,double tolerance=0.00001){return std::abs(a-b)<tolerance;}
struct V3{float x,y,z;};
struct Matrix{V3 right;unsigned flags;V3 up;unsigned p1;V3 at;unsigned p2;V3 pos;unsigned p3;};
float Dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(V3 a){return std::sqrt(Dot(a,a));}
int main(){try{
    Require(!Settings{}.buildings,"building mode must be opt-in");
    for(const char* name:{"hotel01","nb_apartments","bankfront","LODhotel01"})
        Require(BuildingCandidate(1,false,false,name),"building model omitted");
    for(const char* name:{"miamiland037","miamiland170","miamiland171","miamiland172","miamiland173","miamiland174","miamiland175","miamiland179","miamiland180","LODmiLand037","LODmiLand170","LODmiLand171","LODmiLand172","LODmiLand173","LODmiLand174","LODmiLand175","LODmiLand179","LODmiLand180","lhcoastsky1","lhcoastsky2","LODoastsky1","LODoastsky2"})
        Require(!TerrainModelName(name)&&BuildingCandidate(1,false,false,name),"Little Havana house/tower mistaken for ground");
    for(const char* name:{"lithavabit03","lithavabit04","havbit01","havbit02","LODhavabit04","LODbit02","miamiland176","miamiland177","miamiland178","LODmiLand176","LODmiLand177","LODmiLand178"})
        Require(TerrainModelName(name)&&!BuildingCandidate(1,false,false,name)&&!VegetationModelName(name),"Little Havana garden/road joins moving scenery");
    for(const char* name:{"road01","dock_ground","streetlamp2","bench01","trafficlight","tree_shadow","bridge01"})
        Require(!BuildingCandidate(1,false,false,name),"building mode selected road/prop/shadow");
    Require(!BuildingCandidate(1,true,false,"hotel01")&&!BuildingCandidate(1,false,true,"hotel01")&&!BuildingCandidate(2,false,false,"hotel01"),"buildings overlapped plants, roads or transport");
    Settings buildingSettings;buildingSettings.enabled=false;buildingSettings.vegetation=false;
    Require(Near(BuildingScale(.5,buildingSettings,20).horizontal,1)&&Near(BuildingScale(.5,buildingSettings,20).vertical,.94),"building scale must be independent and vertical only");
    Require(Near(BuildingScale(.5,buildingSettings,2).vertical,.94),"short building parts must not be omitted");
    for(const char* name:{"od_clevelander_dy","LODclevelander_nt","deco_polgrnda16","LODo_polgrnda16","od_wt","od_wtlegs","od_wtsign"}) {
        Require(!TerrainModelName(name)&&BuildingCandidate(1,false,false,name),"body/detail mistakenly classified as ground or street furniture");
    }
    for(const char* name:{"dt_stadiumland_a","dt_stadiumland_b","dt_stadiumland_c","dt_stadiumland_d","LODstadiumland_a","LODstadiumland_b","LODstadiumland_c","LODstadiumland_d","dt_stadium_start","dt_stadium_entrance","dt_stadium__alfa1","doontoon03","doontoon19","doontoon24","doontoon29","doontoon_top06"})
        Require(BuildingCandidate(1,false,false,name),"Downtown body or stadium mistaken for terrain");
    for(const char* name:{"doontoon09","doontoon11","doontoon59","doontoon61","doontoon62","doontoon63","doontoon67","doontoon70","dt_hotel_waldt","LODntoon09","LODntoon11","LODntoon59","LODntoon61","LODntoon62","LODntoon63","LODntoon67","LODntoon70","LODhotel_waldt","dt_carpsaces13","dt_carpsaces14","dtoon62_spaces"}) {
        Require(TerrainModelName(name),"neutral Downtown pavement not usable as ground");
        Require(!BuildingCandidate(1,false,false,name)&&!VegetationModelName(name),"Downtown pavement joins moving building groups");
    }
    const auto stadiumProbes=SceneryGroundProbes({-150,-100,0,150,100,96});
    Require(Near(stadiumProbes[5].x,0)&&Near(stadiumProbes[5].y,-100.75),"long building lacks street-facing edge probe");
    Require(Near(stadiumProbes[9].x,-75)&&Near(stadiumProbes[9].y,-50),"stadium courtyard probe missing");
    const auto coastalProbes=SceneryGroundProbes({-1596.85f,1128.80f,-11.26f,-980.66f,1533.23f,84.96f});
    // Verified stock road collision exists near this strip; center/corners of
    // the stadium's footprint fall into holes or water instead.
    Require(Near(coastalProbes[22].x,-979.91,.001)&&Near(coastalProbes[22].y,1381.56875,.001),"stadium narrow eastern pavement fallback missing");
    for(const char* name:{"streetlamp1","trafficlight1","rd_sign01","trashbin1","parkmeter01","bench01"}) {
        Require(StreetObjectName(SceneryName(name))&&!BuildingCandidate(1,false,false,name),"street category overlaps buildings or omits furniture");
    }
    for(const char* name:{"fencesmallb","tall_fence","electricgate","columbiangate","comgate2open","comgate1closed","LODgate2","man1railing"})
        Require(StreetObjectName(SceneryName(name))&&!BuildingCandidate(1,false,false,name),"fence/gate/railing missing from independent F3 group");
    for(const char* name:{"veg_palwee01","veg_palwee02","veg_palwee03","veg_palmkb5","veg_palmkb8","veg_palmbig14"})
        Require(VegetationModelName(name),"stock palm variant missing from F5 group");
    for(const char* name:{"veg_palm04","veg_palmkb1","veg_palmkb2","veg_palmkb3","veg_palmkb4","veg_palmkb9","veg_palmkb7","veg_palmkb10","veg_palmkbb11","veg_palmkb13","veg_palmkb14","veg_palm01","washskyplant1","washskyplant2","wshotelveg1","nrth7veg","nrth1veg21","nrth3veg59","nrth4veg212","washpshoutveg","nbwbusshy2"}) {
        Require(VegetationModelName(name),"eastern island foliage omitted from F5");
        Require(!BuildingCandidate(1,VegetationModelName(name),false,name),"eastern island foliage overlaps buildings");
    }
    Require(!StreetObjectName("wshotelsign35")&&!StreetObjectName("od_wtsign"),"attached sign must follow its structure");
    Require(!Settings{}.streetObjects,"street objects should be opt-in");
    const std::vector<SceneryBounds> pieces{{0,0,0,10,10,20},{10,0,0,20,10,20},{19,4,12,21,6,14},{100,100,0,110,110,30},{-5,-5,0,25,15,20}};
    const auto groups=GroupScenery(pieces,{true,true,false,true,false},{true,true,true,true,false});
    Require(Near(groups[0].z0,groups[2].z0)&&Near(groups[0].z1,groups[2].z1)&&Near(groups[0].x1,21),"building detail did not share body transform");
    Require(Near(groups[3].x0,100)&&Near(groups[3].z1,30),"detail or LOD joined separate buildings");
    const std::vector<SceneryBounds> coastalParts{{0,0,-11,100,100,85},{100,0,5,120,100,25},{98,5,8,102,10,20},{0,0,-12,120,100,84},{500,500,0,510,510,30}};
    const std::vector<bool> coastalCore{true,true,false,false,true};
    const auto coastalGroups=GroupScenery(coastalParts,coastalCore,{true,true,true,false,true});
    const auto supports=SharedSceneryGround(coastalParts,coastalGroups,coastalCore,[](const SceneryBounds& b,float& ground){
        // Only the front structure's courtyard probes hit a real road.
        if(b.x0==100&&b.x1==120){ground=7.7f;return true;}return false;
    });
    for(int i:{0,1,2,3}) Require(supports[i].found&&Near(supports[i].z,7.7),"coastal body/detail/LOD lost common support from a member footprint");
    Require(!supports[4].found,"missing ground borrowed from an unrelated building");
    const auto tower=GroupScenery({{0,0,20,10,10,25},{0,0,0,10,10,20},{2,2,18,3,3,22}},{true,false,false});
    Require(Near(tower[0].z0,0)&&Near(tower[1].z0,0)&&Near(tower[2].z1,25),"tower legs/tank/details lost common ground anchor");
    const auto firstScale=BuildingScale(.5,buildingSettings,groups[0].z1-groups[0].z0);
    const auto detailScale=BuildingScale(.5,buildingSettings,groups[2].z1-groups[2].z0);
    const float seam=13;
    Require(Near(groups[0].z0+(seam-groups[0].z0)*firstScale.vertical,groups[2].z0+(seam-groups[2].z0)*detailScale.vertical),"building/detail seam separated");
    Require(Near(StreetScale(.5,buildingSettings,2).horizontal,1)&&Near(StreetScale(.5,buildingSettings,2).vertical,.8),"independent street deformation failed");
    Require(1000*std::abs(BuildingScale(.5,buildingSettings,1000).vertical-1)<=4.0001,"skyscraper exceeds building travel cap");
    for(const char* name:{"nbecland01","nbmiamiland068","LODiamiland068","LODecland01","grasspatch","lhgrasspatchg","dockgrass","nbhospgrnd","beachblend01","nbeachb1","sjmgrass2"}) {
        Require(TerrainModelName(name),"terrain/lawn mesh was not recognized");
        Require(!BuildingCandidate(1,false,false,name)&&!VegetationModelName(name),"terrain/lawn affected by F4 or F5");
    }
    for(const char* name:{"veg_palm04","kb_planter+bush2","gf_hedge1_01","ht_veg02_dy","nhaitiplnt14","plants05b","drive_flowers06","LODmrgtREes4","bank_palms02","veg_ivy_balcny_kb3","club_exterior08","club_exterior09","club_exterior11","club_exterior12","man_maze"})
        Require(VegetationModelName(name),"non-wind vegetation model omitted");
    for(const char* name:{"Streetlamp2","doublestreetlght1","infernus","treeshadowgolf06","treeshadnbt01","svegrgedoor","dock_grassarea","LODK_grassarea","gf_golfwall","golf_gatesclosed","dts_bbdoor"})
        Require(!VegetationModelName(name),"non-vegetation scenery incorrectly selected");
    Require(!VegetationModelName(nullptr),"null vegetation name accepted");
    const char boundedName[5]={'b','u','s','h','x'};
    Require(VegetationModelName(boundedName,5),"bounded model name was not read safely");
    Settings softerPlants;softerPlants.enabled=false;
    Settings originalScenery,boostedScenery;
    boostedScenery.vegetationStrength=originalScenery.vegetationStrength*1.13;
    boostedScenery.vegetationMaxDisplacement=originalScenery.vegetationMaxDisplacement*1.13;
    boostedScenery.buildingStrength=originalScenery.buildingStrength*1.13;
    boostedScenery.buildingMaxDisplacement=originalScenery.buildingMaxDisplacement*1.13;
    for(double height:{2.,20.,40.,1000.}) for(int i=0;i<100;++i) {
        Require(Near(VegetationScale(i/100.,boostedScenery,height).vertical-1,
            (VegetationScale(i/100.,originalScenery,height).vertical-1)*1.13),"plant boost must also affect travel-limited tall trees");
        Require(Near(BuildingScale(i/100.,boostedScenery,height).vertical-1,
            (BuildingScale(i/100.,originalScenery,height).vertical-1)*1.13),"building boost must also affect travel-limited towers");
    }
    for(int i=0;i<100;++i) {
        const auto soft=VegetationScale(i/100.,softerPlants,2);
        auto global=softerPlants;global.enabled=true;
        const auto full=GetScale(i/100.,global);
        Require(Near(soft.horizontal,1)&&Near(soft.vertical-1,(full.vertical-1)*.275),"vegetation must use fixed XY and 27.5 percent strength");
    }
    softerPlants.vegetationStrength=0;
    Require(Near(VegetationScale(.5,softerPlants,2).vertical,1),"zero vegetation strength did not disable deformation");
    softerPlants.vegetationStrength=1;
    softerPlants.vegetationMaxDisplacement=1;
    Require(Near(VegetationScale(.5,softerPlants,2).vertical,.8),"full vegetation strength should match global amplitude when below cap");
    // A sheared, elevated tree has a root below its model origin. Its bottom
    // footprint center must stay fixed in XYZ through every beat and phase.
    const Matrix windTree{{1,0,0},0,{0,1,0},0,{.15f,-.22f,1},0,{17,31,7},0};
    auto worldPoint=[](const Matrix& m,V3 p){return V3{
        m.pos.x+m.right.x*p.x+m.up.x*p.y+m.at.x*p.z,
        m.pos.y+m.right.y*p.x+m.up.y*p.y+m.at.y*p.z,
        m.pos.z+m.right.z*p.x+m.up.z*p.y+m.at.z*p.z};};
    auto compose=[&](const Matrix& parent,const Matrix& local){
        Matrix result=local;
        auto direction=[&](V3 v){auto p=worldPoint(parent,v);p.x-=parent.pos.x;p.y-=parent.pos.y;p.z-=parent.pos.z;return p;};
        result.right=direction(local.right);result.up=direction(local.up);result.at=direction(local.at);result.pos=worldPoint(parent,local.pos);
        return result;
    };
    const Matrix identity{{1,0,0},0,{0,1,0},0,{0,0,1},0,{0,0,0},0};
    Matrix roadBasis=identity;roadBasis.pos={0,0,10};
    for(int step=0;step<100;++step) {
        const auto scale=BuildingScale(step/100.,buildingSettings,40);
        const auto base=DeformPosition(V3{3,7,10},roadBasis,scale);
        const auto basement=DeformPosition(V3{3,7,-15},roadBasis,scale);
        const auto bulb=DeformPosition(V3{3,7,30},roadBasis,scale);
        const auto mesh=DeformedMatrix(identity,roadBasis,scale);
        const auto meshBulb=worldPoint(mesh,{3,7,30});
        Require(Near(base.z,10)&&Near(base.x,3)&&Near(base.y,7),"visible road contact moved with buried foundation");
        Require(basement.z<10,"underground foundation protruded above ground");
        Require(Near(meshBulb.x,bulb.x)&&Near(meshBulb.y,bulb.y)&&Near(meshBulb.z,bulb.z),"halo and mesh bulb transforms diverged");
    }
    const Matrix rotatedParent{{0,2,0},0,{0,0,3},0,{4,0,0},0,{25,17,9},0};
    const Matrix rotatedPlant{{0,0,1},0,{1,0,0},0,{0,1,0},0,{7,11,5},0};
    const std::array<V3,5> group{{{-20,-10,-4},{30,20,-4},{10,-7,24},{-5,12,8},{0,0,3}}};
    for(const auto& parent:{identity,rotatedParent}) {
        const auto world=compose(parent,rotatedPlant);
        VegetationAnchor bounds;for(auto p:group) bounds.Add(worldPoint(world,p));
        const auto basis=bounds.Basis(world);
        for(int i=0;i<100;++i) {
            const auto scale=BuildingScale(i/100.,buildingSettings,bounds.top-bounds.bottom);
            Matrix local{};
            Require(VegetationLocalMatrix(DeformedMatrix(world,basis,scale),parent,local),"building parent transform failed");
            const auto rendered=compose(parent,local);
            for(auto p:group) {
                const auto before=worldPoint(world,p),after=worldPoint(rendered,p);
                Require(Near(before.x,after.x,.0001)&&Near(before.y,after.y,.0001),"rotated building footprint changed");
                Require(Near(after.z,bounds.bottom+(before.z-bounds.bottom)*scale.vertical,.0001),"building deformation did not use grounded world Z");
            }
        }
    }
    for(const auto& parent:{identity,rotatedParent}) for(const auto& local:{windTree,rotatedPlant}) {
        const auto world=compose(parent,local);
        VegetationAnchor bounds;for(auto p:group) bounds.Add(worldPoint(world,p));
        const auto basis=bounds.Basis(world);
        Settings plants;plants.vegetationMaxDisplacement=.12;
        for(int step=0;step<100;++step) {
            const auto scale=VegetationScale(step/100.,plants,bounds.top-bounds.bottom);
            const auto desired=DeformedMatrix(world,basis,scale);
            Matrix adjusted{};
            Require(VegetationLocalMatrix(desired,parent,adjusted),"valid parent matrix rejected");
            const auto actual=compose(parent,adjusted);
            for(auto p:group) {
                const auto before=worldPoint(world,p),after=worldPoint(actual,p);
                Require(Near(before.x,after.x,.0001)&&Near(before.y,after.y,.0001),"rotated/grouped vegetation drifted sideways");
                Require(std::abs(after.z-before.z)<=.1201,"large vegetation exceeded 12cm peak travel");
                Require(Near(after.z,bounds.bottom+(before.z-bounds.bottom)*scale.vertical,.0001),"plant used model axis instead of world Z");
            }
        }
    }
    for(double height:{.25,2.,10.,50.,100.}) {
        Settings plants;plants.vegetationMaxDisplacement=.12;
        const auto scale=VegetationScale(.5,plants,height);
        Require(Near(scale.horizontal,1)&&height*std::abs(scale.vertical-1)<=.12001,"vegetation absolute travel cap failed");
        if(height>=2) Require(Near(scale.vertical,1-std::min(.2*.275,.12/height)),"vegetation percentage or explicit travel cap failed");
    }
    Matrix invalid=identity;invalid.at={0,0,0};Matrix ignored{};
    Require(!VegetationLocalMatrix(identity,invalid,ignored),"singular parent must be skipped");
    VegetationAnchor plantAnchor;
    Require(!plantAnchor.Valid(),"empty vegetation mesh must be skipped");
    for(V3 p:std::array<V3,5>{{{-1,-1,-4},{1,-1,-4},{-1,1,-4},{1,1,-4},{0,0,8}}}) plantAnchor.Add(worldPoint(windTree,p));
    plantAnchor.Add(V3{NAN,0,0});
    const auto plantBasis=plantAnchor.Basis(windTree);
    const auto root=worldPoint(windTree,{0,0,-4});
    Require(Near(plantBasis.pos.x,root.x)&&Near(plantBasis.pos.y,root.y)&&Near(plantBasis.pos.z,root.z),"vegetation pivot is not the bottom footprint center");
    Settings plantSettings;plantSettings.vegetation=true;
    for(int step=0;step<100;++step) {
        const auto scale=GetScale(step/100.,plantSettings);
        const auto result=DeformedMatrix(windTree,plantBasis,scale);
        const auto fixed=worldPoint(result,{0,0,-4});
        Require(Near(fixed.x,root.x)&&Near(fixed.y,root.y)&&Near(fixed.z,root.z),"vegetation root moved during squash/stretch");
        const auto top=worldPoint(result,{0,0,8});
        Require(Near(top.z-root.z,12*scale.vertical),"vegetation height did not follow beat");
        Require(Near(result.at.x,windTree.at.x*scale.horizontal)&&Near(result.at.y,windTree.at.y*scale.horizontal),"native vegetation wind shear was lost");
    }
    Require(!Settings{}.vegetation,"vegetation must be opt-in");
    BeatMap beatMap;
    std::istringstream beats("ViceBounceBeats 1\n370\n870\n1320\n1720\n");
    Require(beatMap.Load(beats)&&beatMap.Size()==4,"variable tempo map failed to load");
    for(double beat:{370.,870.,1320.,1720.}) Require(Near(beatMap.Phase(beat),.5),"beat map missed squash peak");
    for(double half:{620.,1095.,1520.}) Require(Near(beatMap.Phase(half),0),"variable beat midpoint missed stretch peak");
    Require(Near(beatMap.Phase(-130),.5)&&Near(beatMap.Phase(2120),.5),"intro/outro extrapolation failed");
    Require(Near(beatMap.Nearest(900),870)&&Near(beatMap.Nearest(1200),1320),"manual map phase correction selected wrong beat");
    for(const char* invalid:{"", "ViceBounceBeats 1\n100\n", "ViceBounceBeats 1\n200\n100\n", "ViceBounceBeats 1\n0\n500\njunk\n", "ViceBounceBeats 1\n0\n50\n", "ViceBounceBeats 1\n0\nnan\n", "ViceBounceBeats 1\n0\n500\n1e999"}) {
        std::istringstream stream(invalid);Require(!beatMap.Load(stream)&&!beatMap.Valid(),"malformed beat map was accepted");
    }
    std::istringstream crlf("ViceBounceBeats 1\r\n0\r\n500\r\n");Require(beatMap.Load(crlf),"Windows CRLF beat map rejected");
    Require(ParseNumber("nan",120,20,300)==120,"NaN config must fall back");
    Require(ParseNumber("inf",120,20,300)==120,"infinite config must fall back");
    Require(ParseNumber("100garbage",120,20,300)==120,"malformed config must fall back");
    Require(ParseNumber("1e999",120,20,300)==120,"overflow config must fall back");
    Require(ParseNumber("0",120,20,300)==20,"BPM minimum clamp");
    Require(ParseNumber("999",120,20,300)==300,"BPM maximum clamp");
    Require(ParseNumber(" 125 \t",120,20,300)==125,"whitespace config");
    Require(Near(AdvancePhase(0,0.5,120),0),"120 BPM period");
    Require(Near(AdvancePhase(0.25,0,180),0.25),"changing BPM preserves phase");
    const double nan=std::numeric_limits<double>::quiet_NaN();
    Require(std::isfinite(AdvancePhase(nan,0.1,120)),"phase sanitization");
    Settings settings;
    for(double offset:{0.0,173.0,1200.0}) for(int beat=0;beat<1000;++beat) {
        const double position=offset+beat*60000.0/135.0;
        const auto onBeat=GetScale(MusicPhase(position,offset,135),settings);
        Require(Near(onBeat.horizontal,1.2)&&Near(onBeat.vertical,.8),"music beat does not land on maximum squash");
        const auto halfway=GetScale(MusicPhase(position+30000.0/135.0,offset,135),settings);
        Require(Near(halfway.horizontal,.8)&&Near(halfway.vertical,1.2),"half beat does not land on maximum stretch");
    }
    Require(Near(MusicPhase(0,173,135),MusicPhase(60000.0/135.0,173,135)),"negative pre-beat phase or track loop invalid");
    Require(Near(AdvancePhase(0,1,135),0.25),"135 BPM must run at 2.25 cycles per second");
    for(bool cars:{false,true}) for(bool other:{false,true}) {
        settings.enabled=cars;settings.otherVehicles=other;
        Require(VehicleEnabled(settings,0)==cars,"F6 car group must be independent");
        for(int kind:{1,3,4,5}) Require(VehicleEnabled(settings,kind)==other,"F8 other transport group must be independent");
        Require(!VehicleEnabled(settings,-1)&&!VehicleEnabled(settings,2)&&!VehicleEnabled(settings,6),"unsupported vehicle kind selected");
    }
    settings=Settings{};
    for(bool people:{false,true}) for(bool vehicles:{false,true}) for(bool seated:{false,true}) {
        const int expected=vehicles&&seated?2:(people||vehicles?1:0);
        Require(PedEffect(people,vehicles,seated)==expected,"independent all-people mode or seated vehicle precedence");
    }
    for(int easing=0;easing<4;++easing){settings.easing=easing;
        for(int i=0;i<10000;++i){double p=i/10000.0;auto scale=GetScale(p,settings);
            Require(std::isfinite(scale.horizontal)&&scale.horizontal>=0.8-1e-6&&scale.horizontal<=1.2+1e-6,"horizontal scale bounds");
            Require(std::isfinite(scale.vertical)&&scale.vertical>=0.8-1e-6&&scale.vertical<=1.2+1e-6,"vertical scale bounds");
            Require(Near(scale.horizontal+scale.vertical,2),"opposing squash/stretch");
            const auto periodic=GetScale(p+1,settings);Require(Near(scale.horizontal,periodic.horizontal),"periodicity");
        }
    }
    const float h=std::sqrt(0.5f);
    Matrix original{{h,h,0},0x20003,{-h,h,0},0,{0,0,1},0,{27,-45,12},0};
    const auto snapshot=original;
    auto scaled=ScaledMatrix(original,{1.2f,0.8f});
    Require(std::memcmp(&snapshot,&original,sizeof(Matrix))==0,"source matrix mutated");
    Require(std::memcmp(&scaled.pos,&original.pos,sizeof(V3))==0,"world position changed");
    Require(Near(Length(scaled.right),1.2)&&Near(Length(scaled.up),1.2)&&Near(Length(scaled.at),0.8),"local axes scale");
    Require(Near(Dot(scaled.right,scaled.up),0),"orientation skew");
    Require((scaled.flags&0x20003)==0,"stale RenderWare flags");
    scaled=original;Require(std::memcmp(&scaled,&snapshot,sizeof(Matrix))==0,"exact restoration");
    // Occupants and lights share B*S*inverse(B), including a rolled/pitched car.
    for(int i=0;i<1000;++i) {
        const float angle=float(i*0.007),c=std::cos(angle),s=std::sin(angle);
        Matrix car{{1,0,0},0,{0,c,s},0,{0,-s,c},0,{100,-250,37},0};
        const V3 seat{car.pos.x+2,car.pos.y+3*c-s,car.pos.z+3*s+c};
        const auto transformed=DeformPosition(seat,car,{1.2f,0.8f});
        Require(Near(transformed.x,car.pos.x+2.4,0.00004)&&
                Near(transformed.y,car.pos.y+3.6*c-0.8*s,0.00004)&&
                Near(transformed.z,car.pos.z+3.6*s+0.8*c,0.00004),"occupant/light position in tilted car");
        auto bone=original;bone.pos=seat;
        const auto deformed=DeformedMatrix(bone,car,{1.2f,0.8f});
        Require(Near(deformed.pos.x,transformed.x)&&Near(deformed.pos.y,transformed.y)&&Near(deformed.pos.z,transformed.z),"bone and light anchors diverged");
        const auto vertical=DeformDirection(car.at,car,{1.2f,0.8f});
        Require(Near(Length(vertical),0.8),"light vertical extent in tilted car");
        const auto returned=DeformPosition(transformed,car,{1/1.2f,1/0.8f});
        Require(Near(returned.x,seat.x,0.00006)&&Near(returned.y,seat.y,0.00006)&&Near(returned.z,seat.z,0.00006),"deformation inverse");
        const auto center=DeformPosition(car.pos,car,{1.2f,0.8f});
        Require(std::memcmp(&center,&car.pos,sizeof(V3))==0,"vehicle pivot moved");
    }
    auto nonUnit=ScaledMatrix(original,{2,3});
    const auto lightAxis=DeformDirection(nonUnit.at,nonUnit,{1.2f,0.8f});
    Require(Near(Length(lightAxis),2.4),"non-unit reference basis inverse");
    Matrix singular{};const V3 input{2,3,4};const auto guarded=DeformDirection(input,singular,{1.2f,0.8f});
    Require(std::memcmp(&input,&guarded,sizeof(V3))==0,"singular vehicle matrix fallback");
    GroundAnchor empty;Require(empty.Offset()==0,"missing wheel geometry fallback");
    GroundAnchor flat;flat.Add(-0.75f,-0.9f);
    Require(Near(flat.Offset(),0.15),"vertical stretch must raise vehicle");
    GroundAnchor compressed;compressed.Add(-0.75f,-0.6f);
    Require(Near(compressed.Offset(),-0.15),"compression must keep tyres grounded");
    for(int i=0;i<1000;++i) {
        const float angle=float(i*0.009),c=std::cos(angle),s=std::sin(angle);
        Matrix car{{1,0,0},0,{0,c,s},0,{0,-s,c},0,{30,40,10},0};
        const Scale scale{float(1+0.7*std::sin(i*0.04)),float(1-0.7*std::sin(i*0.04))};
        GroundAnchor ground;
        for(float x:{-0.8f,0.8f}) for(float y:{-1.2f,1.2f}) for(int n=0;n<64;++n) {
            const float t=float(n*6.28318530718/64),localY=y+0.35f*std::cos(t),localZ=-0.4f+0.35f*std::sin(t);
            V3 p{car.pos.x+x,car.pos.y+c*localY-s*localZ,car.pos.z+s*localY+c*localZ};
            ground.Add(p.z,DeformPosition(p,car,scale).z);
        }
        Require(Near(ground.deformed+ground.Offset(),ground.original,0.000003),"tyre lower extent changed with squash/stretch");
        // Every tyre vertex remains above the preserved original lower extent.
        for(float x:{-0.8f,0.8f}) for(float y:{-1.2f,1.2f}) for(int n=0;n<64;++n) {
            const float t=float(n*6.28318530718/64),localY=y+0.35f*std::cos(t),localZ=-0.4f+0.35f*std::sin(t);
            V3 p{car.pos.x+x,car.pos.y+c*localY-s*localZ,car.pos.z+s*localY+c*localZ};
            Require(DeformPosition(p,car,scale).z+ground.Offset()>=ground.original-0.000003f,"deformed tyre penetrates anchor plane");
        }
    }
    // An actor beside a car keeps its own world pivot and lowest skeleton point.
    for(int i=0;i<1000;++i) {
        Matrix actor=original;actor.pos={float(30+i*.001),40,1};
        const Scale scale{float(1+.7*std::sin(i*.03)),float(1-.7*std::sin(i*.03))};
        GroundAnchor feet;
        const V3 foot{actor.pos.x,actor.pos.y,0},head{actor.pos.x,actor.pos.y,1.8f};
        for(auto p:{foot,head}) feet.Add(p.z,DeformPosition(p,actor,scale).z);
        auto transformed=DeformPosition(foot,actor,scale);transformed.z+=feet.Offset();
        Require(Near(transformed.x,foot.x)&&Near(transformed.y,foot.y)&&Near(transformed.z,foot.z),"standing actor moved away from own ground pivot");
    }
    // Cutscene entity origins are shared; animated roots carry individual motion.
    // Mesh axes can be rotated 90 degrees (e.g. chairs) without changing bounce axis.
    for(int i=0;i<1000;++i) {
        Matrix scene{{1,0,0},0,{0,0,1},0,{0,-1,0},0,{500,-200,4},0};
        V3 movingRoot{float(500+i*.1),float(-190+i*.03),float(20+i*.01)};
        const auto basis=UprightActorBasis(scene,movingRoot);
        const auto center=DeformPosition(movingRoot,basis,{1.2f,.8f});
        Require(Near(center.x,movingRoot.x)&&Near(center.y,movingRoot.y)&&Near(center.z,movingRoot.z),"animated aircraft trajectory changed by scene pivot");
        const auto vertical=DeformDirection(V3{0,0,2},basis,{1.2f,.8f});
        Require(Near(vertical.x,0)&&Near(vertical.y,0)&&Near(vertical.z,1.6),"rotated chair stretched along mesh axis instead of world vertical");
        const V3 foot{movingRoot.x,movingRoot.y,movingRoot.z-1};
        GroundAnchor ground;ground.Add(foot.z,DeformPosition(foot,basis,{1.2f,.8f}).z);
        auto grounded=DeformPosition(foot,basis,{1.2f,.8f});grounded.z+=ground.Offset();
        Require(Near(grounded.x,foot.x)&&Near(grounded.y,foot.y)&&Near(grounded.z,foot.z),"walking cutscene actor follows scene pivot");
    }
    // The prop handle and hand share one affine transform, including ground lift.
    // Copying the transformed bone to a weapon also yields precisely A*(H*p).
    for(int i=0;i<1000;++i) {
        const auto basis=UprightActorBasis(original,V3{float(i*.1),40,1});
        const Scale scale{float(1+.7*std::sin(i*.03)),float(1-.7*std::sin(i*.03))};
        Matrix hand=original;hand.pos={basis.pos.x+.6f,basis.pos.y+.3f,1.2f};
        const V3 grip{hand.pos.x+hand.right.x*.2f,hand.pos.y+hand.right.y*.2f,hand.pos.z+hand.right.z*.2f};
        auto movedHand=DeformedMatrix(hand,basis,scale);movedHand.pos.z+=.17f;
        V3 weaponGrip{movedHand.pos.x+movedHand.right.x*.2f,movedHand.pos.y+movedHand.right.y*.2f,movedHand.pos.z+movedHand.right.z*.2f};
        auto propGrip=DeformPosition(grip,basis,scale);propGrip.z+=.17f;
        Require(Near(weaponGrip.x,propGrip.x,.00002)&&Near(weaponGrip.y,propGrip.y,.00002)&&Near(weaponGrip.z,propGrip.z,.00002),"held prop detached from deformed hand");
        const auto savedBasis=basis;
        auto ownerRendered=DeformedMatrix(hand,basis,scale);
        Require(std::memcmp(&savedBasis,&basis,sizeof(Matrix))==0,"owner rendering mutated attachment basis");
        const auto propFirst=DeformPosition(grip,savedBasis,scale);
        const auto ownerFirst=DeformPosition(grip,basis,scale);
        Require(Near(propFirst.x,ownerFirst.x)&&Near(propFirst.y,ownerFirst.y)&&Near(propFirst.z,ownerFirst.z),"attachment depends on render order");
    }
    for(int state:{0,1,24,25,50,54,55}) Require(!VehicleInteractionState(state),"stale association eligible outside interaction state");
    Require(RenderableObjectType(1)&&RenderableObjectType(2)&&!RenderableObjectType(0),"atomic Airtrain and clump render selection");
    Require(!VisualPaused(false,true,false,true),"cutscene code pause stopped visual effect");
    Require(VisualPaused(true,true,false,true),"user pause ignored inside cutscene");
    Require(VisualPaused(false,true,false,false),"ordinary code pause ignored");
    Require(!DoorInteractionActive(24,76)&&!DoorInteractionActive(58,74)&&!DoorInteractionActive(58,91),"approach/alignment started bounce early");
    for(int anim:{76,77,78,80,82,92,94,96,98}) Require(DoorInteractionActive(58,anim),"door/open/entry animation excluded");
    Require(DoorInteractionActive(56,72)&&DoorInteractionActive(60,127),"carjack/exit door interaction excluded");
    Require(!DoorInteractionActive(58,131),"unrelated standing animation bounced");
    settings.enabled=false;auto disabled=GetScale(0.5,settings);Require(disabled.horizontal==1&&disabled.vertical==1,"disabled identity");
    settings.enabled=true;settings.amplitude=nan;auto finite=GetScale(nan,settings);Require(std::isfinite(finite.horizontal)&&std::isfinite(finite.vertical),"invalid input leaked to render");
    std::cout<<"PASS: independent car/other transport toggles, 135 BPM timing, config validation, 40000 phase samples, exact restoration, 1000 occupant/light transforms and 1000 grounded wheel meshes\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
