#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <limits>

namespace vicebounce {
struct Settings { double bpm=135.0; double amplitude=0.20; int easing=1; bool enabled=true; bool otherVehicles=true; bool allPeople=false; bool buildings=false; };
inline int PedEffect(bool allPeople,bool vehicleActive,bool inVehicle) {
    return vehicleActive&&inVehicle?2:(allPeople||vehicleActive?1:0);
}
inline bool VehicleEnabled(const Settings& settings,int kind) {
    return kind==0?settings.enabled:(kind==1||kind==3||kind==4||kind==5)&&settings.otherVehicles;
}
inline bool RenderableObjectType(int type) { return type==1||type==2; } // rpATOMIC/rpCLUMP
inline bool VisualPaused(bool userPause,bool codePause,bool replay,bool cutscene) {
    return userPause||(codePause&&!replay&&!cutscene);
}
inline bool VehicleInteractionState(int state) { return state==56||state==58||state==60; }
inline bool DoorInteractionActive(int state,int animation) {
    if(!VehicleInteractionState(state)) return false;
    return animation==72||(animation>=76&&animation<=89)||
           (animation>=92&&animation<=101)||(animation>=119&&animation<=124)||
           (animation>=127&&animation<=129);
}
inline double ParseNumber(const char* text, double fallback, double lo, double hi) {
    if (!text || !*text) return fallback;
    char* end=nullptr;
    const double value=std::strtod(text,&end);
    if (end==text || !std::isfinite(value)) return fallback;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) return fallback;
    return std::clamp(value,lo,hi);
}
inline double Ease(double x,int type) {
    x=std::clamp(x,0.0,1.0);
    if(type==1) return (1.0-std::cos(3.14159265358979323846*x))*0.5;
    if(type==2) return x<0.5 ? 2*x*x : 1-std::pow(-2*x+2,2)*0.5;
    if(type==3) {
        auto bounce=[](double t){
            constexpr double n=7.5625,d=2.75;
            if(t<1/d) return n*t*t;
            if(t<2/d){t-=1.5/d;return n*t*t+0.75;}
            if(t<2.5/d){t-=2.25/d;return n*t*t+0.9375;}
            t-=2.625/d;return n*t*t+0.984375;
        };
        return x<0.5 ? (1-bounce(1-2*x))*0.5 : (1+bounce(2*x-1))*0.5;
    }
    return x;
}
inline double AdvancePhase(double phase,double seconds,double bpm) {
    if(!std::isfinite(phase)) phase=0;
    if(!std::isfinite(seconds)||seconds<0||!std::isfinite(bpm)) return phase;
    phase=std::fmod(phase+seconds*std::clamp(bpm,20.0,300.0)/60.0,1.0);
    return phase<0 ? phase+1 : phase;
}
inline double MusicPhase(double positionMs,double firstBeatMs,double bpm) {
    // Phase .5 is widest/lowest: every beat lands at maximum squash.
    if(!std::isfinite(positionMs)||!std::isfinite(firstBeatMs)||!std::isfinite(bpm)) return .5;
    double result=std::fmod((positionMs-firstBeatMs)*std::clamp(bpm,20.0,300.0)/60000.0+.5,1.0);
    return result<0?result+1:result;
}
struct Scale { float horizontal,vertical; };
template<class Matrix,class Vector> Matrix UprightActorBasis(Matrix basis,const Vector& pivot) {
    basis.right={1,0,0};basis.up={0,1,0};basis.at={0,0,1};
    basis.pos={pivot.x,pivot.y,pivot.z};basis.flags=0;
    return basis;
}
struct GroundAnchor {
    float original=std::numeric_limits<float>::infinity();
    float deformed=std::numeric_limits<float>::infinity();
    void Add(float before,float after) {
        if(std::isfinite(before)&&std::isfinite(after)) {
            original=std::min(original,before);deformed=std::min(deformed,after);
        }
    }
    bool Valid() const { return std::isfinite(original)&&std::isfinite(deformed); }
    float Offset() const { return Valid()?original-deformed:0; }
};
inline Scale GetScale(double phase,const Settings& settings) {
    if(!settings.enabled) return {1,1};
    if(!std::isfinite(phase)) phase=0;
    phase-=std::floor(phase);
    const double t=phase<0.5 ? phase*2 : (1-phase)*2;
    const double centered=2*Ease(t,settings.easing)-1;
    const double amplitude=std::isfinite(settings.amplitude) ? std::clamp(settings.amplitude,0.0,0.75) : 0.20;
    return {float(1+amplitude*centered),float(1-amplitude*centered)};
}
// Copies the original basis; never constructs a new world orientation or position.
// Compatible with both RenderWare matrices and the standalone test matrix.
template<class Matrix> Matrix ScaledMatrix(const Matrix& original,Scale scale) {
    Matrix result=original;
    for(auto* axis : {&result.right,&result.up}) {
        axis->x*=scale.horizontal; axis->y*=scale.horizontal; axis->z*=scale.horizontal;
    }
    result.at.x*=scale.vertical; result.at.y*=scale.vertical; result.at.z*=scale.vertical;
    result.flags &= ~(0x00020000u|0x00000003u); // invalidate identity/normal/orthogonal assumptions
    return result;
}
// B * S * inverse(B): deform world-space vectors along the vehicle's local axes.
template<class Vector,class Matrix> Vector DeformDirection(Vector v,const Matrix& basis,Scale scale) {
    const auto& r=basis.right;const auto& u=basis.up;const auto& a=basis.at;
    const double cx=u.y*a.z-u.z*a.y,cy=u.z*a.x-u.x*a.z,cz=u.x*a.y-u.y*a.x;
    const double determinant=r.x*cx+r.y*cy+r.z*cz;
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-8) return v;
    const double x=(v.x*cx+v.y*cy+v.z*cz)/determinant*scale.horizontal;
    const double y=(v.x*(a.y*r.z-a.z*r.y)+v.y*(a.z*r.x-a.x*r.z)+v.z*(a.x*r.y-a.y*r.x))/determinant*scale.horizontal;
    const double z=(v.x*(r.y*u.z-r.z*u.y)+v.y*(r.z*u.x-r.x*u.z)+v.z*(r.x*u.y-r.y*u.x))/determinant*scale.vertical;
    v.x=float(r.x*x+u.x*y+a.x*z);v.y=float(r.y*x+u.y*y+a.y*z);v.z=float(r.z*x+u.z*y+a.z*z);
    return v;
}
template<class Vector,class Matrix> Vector DeformPosition(Vector p,const Matrix& basis,Scale scale) {
    p.x-=basis.pos.x;p.y-=basis.pos.y;p.z-=basis.pos.z;
    p=DeformDirection(p,basis,scale);
    p.x+=basis.pos.x;p.y+=basis.pos.y;p.z+=basis.pos.z;
    return p;
}
template<class Matrix,class Reference> Matrix DeformedMatrix(Matrix result,const Reference& basis,Scale scale) {
    result.right=DeformDirection(result.right,basis,scale);
    result.up=DeformDirection(result.up,basis,scale);
    result.at=DeformDirection(result.at,basis,scale);
    result.pos=DeformPosition(result.pos,basis,scale);
    result.flags &= ~(0x00020000u|0x00000003u);
    return result;
}
}
