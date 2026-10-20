#include "../src/BounceMath.h"
#include "../src/BeatMap.h"
#include "../src/GameLayoutCheck.h"
#include <sstream>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
using namespace vicebounce;
void Require(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
bool Near(double a,double b,double tolerance=0.00001){return std::abs(a-b)<tolerance;}
struct V3{float x,y,z;};
struct Matrix{V3 right;unsigned flags;V3 up;unsigned p1;V3 at;unsigned p2;V3 pos;unsigned p3;};
float Dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(V3 a){return std::sqrt(Dot(a,a));}

// A fake executable image: lets the layout verifier run without a game, using exactly the code the plugin runs.
struct FakeImage {
    static constexpr uintptr_t base=0x400000;
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x800000,0);
    const unsigned char* At(uintptr_t address) const {Require(address>=base&&address-base+16<=bytes.size(),"layout address outside the image");return &bytes[address-base];}
    unsigned char* Edit(uintptr_t address){return const_cast<unsigned char*>(At(address));}
    uintptr_t Abs(uintptr_t address) const {return address;}
    void Word(uintptr_t address,uint32_t value){std::memcpy(Edit(address),&value,4);}
    void Call(uintptr_t address,uintptr_t target){*Edit(address)=0xE8;Word(address+1,uint32_t(target-(address+5)));}
};
FakeImage MakeImage(const GameLayout& layout){
    FakeImage image;
    for(const VehicleHook* hook:{&layout.automobile,&layout.boat,&layout.heli,&layout.plane,&layout.bike}){image.Word(hook->vtable+12*4,uint32_t(hook->preRender));image.Word(hook->vtable+13*4,uint32_t(hook->render));}
    image.Word(layout.cutsceneVtable+2*4,uint32_t(layout.cutsceneDestructor));
    image.Word(layout.cutsceneVtable+6*4,uint32_t(layout.cutsceneDeleteRw));
    image.Word(layout.cutsceneVtable+13*4,uint32_t(layout.cutsceneRender));
    for(const auto& signature:layout.signatures) std::memcpy(image.Edit(signature.address),signature.bytes,signature.size);
    for(const auto& call:layout.calls) image.Call(call.address,call.target);
    for(const auto& site:layout.lightCallsites) image.Call(site.address,site.target);
    return image;
}
void CheckLayout(const GameLayout& layout){
    const std::string name=layout.name;
    FakeImage image=MakeImage(layout);
    std::string failure;
    Require(VerifyLayout(layout,image,failure),(name+" layout rejects an image built from its own tables: "+failure).c_str());
    Require(layout.lightCallsites.size()==72,"every automobile light callsite must be listed");
    std::set<uintptr_t> sites;
    for(const auto& site:layout.lightCallsites){
        Require(sites.insert(site.address).second,"duplicate light callsite");
        Require(site.address>=0x401000&&site.address<0x670000,"light callsite outside the code section");
        const uintptr_t targets[]={layout.registerCorona,layout.updateCorona,layout.addPointLight,layout.registerBrightLight,layout.storeLightShadow};
        bool known=false;for(uintptr_t target:targets)known|=target==site.target;
        Require(known,"light callsite targets an unknown light function");
    }
    // Any single wrong byte in any checked place must be noticed instead of patched blindly.
    std::vector<uintptr_t> checked;
    for(const VehicleHook* hook:{&layout.automobile,&layout.boat,&layout.heli,&layout.plane,&layout.bike}){checked.push_back(hook->vtable+12*4);checked.push_back(hook->vtable+13*4);}
    for(int slot:{2,6,13}) checked.push_back(layout.cutsceneVtable+slot*4);
    for(const auto& call:layout.calls) checked.push_back(call.address+1);
    for(const auto& site:layout.lightCallsites) checked.push_back(site.address+1);
    for(const auto& signature:layout.signatures) checked.push_back(signature.address);
    for(uintptr_t address:checked){
        FakeImage broken=MakeImage(layout);
        *broken.Edit(address)^=0x40;
        std::string why;
        Require(!VerifyLayout(layout,broken,why)&&!why.empty(),(name+" layout accepted a corrupted image at "+detail::Hex(address)).c_str());
    }
    // The two executables must never accept each other's code.
    const GameLayout& other=&layout==&layoutSteam?layout10EN:layoutSteam;
    std::string otherFailure;
    Require(!VerifyLayout(other,image,otherFailure),"layouts for different executables must not match one another");
}
int main(){try{
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
    CheckLayout(layoutSteam);CheckLayout(layout10EN);
    std::cout<<"PASS: independent car/other transport toggles, 135 BPM timing, config validation, 40000 phase samples, exact restoration, Steam and 1.0 EN executable layouts, 1000 occupant/light transforms and 1000 grounded wheel meshes\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
