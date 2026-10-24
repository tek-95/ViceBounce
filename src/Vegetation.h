#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include "BounceMath.h"
namespace vicebounce {
inline bool TerrainModelName(const char* data,size_t limit=21) {
    if(!data) return false;
    std::string name;
    for(size_t i=0;i<limit&&data[i];++i) name.push_back(char(std::tolower(static_cast<unsigned char>(data[i]))));
    // These stock names contain "land"/"grnd" but are actual structures.
    for(const char* part:{"clevelander","polgrnd","stadiumland"})
        if(name.find(part)!=std::string::npos) return false;
    // Little Havana uses "land"/"coast" for actual houses and towers too.
    for(const char* model:{"miamiland037","miamiland170","miamiland171","miamiland172","miamiland173","miamiland174","miamiland175","miamiland179","miamiland180",
                          "lodmiland037","lodmiland170","lodmiland171","lodmiland172","lodmiland173","lodmiland174","lodmiland175","lodmiland179","lodmiland180",
                          "lhcoastsky1","lhcoastsky2","lodoastsky1","lodoastsky2"})
        if(name==model) return false;
    // These meshes are the ground/garden beds, including their low walls.
    for(const char* model:{"lithavabit03","lithavabit04","havbit01","havbit02","lodhavabit04","lodbit02"})
        if(name==model) return true;
    // Stock Downtown ground slabs use neutral building-like names. Keep them
    // fixed, recognize their collisions as ground, and exclude them from
    // building groups (otherwise an entire block shares the slab's bounds).
    for(const char* model:{"doontoon09","doontoon11","doontoon59","doontoon61","doontoon62","doontoon63","doontoon67","doontoon70","dt_hotel_waldt",
                          "lodntoon09","lodntoon11","lodntoon59","lodntoon61","lodntoon62","lodntoon63","lodntoon67","lodntoon70","lodhotel_waldt",
                          "dt_carpsaces13","dt_carpsaces14","dtoon62_spaces"})
        if(name==model) return true;
    // Ground surfaces are classified as buildings by the engine. This also
    // covers northern beach land/hills and their LODs, plus grass-only meshes.
    for(const char* part:{"land","ground","grnd","terrain","grass","lawn","hill","sand","seabed","beachblend","shore","coast"})
        if(name.find(part)!=std::string::npos) return true;
    return name=="nbeachb"||name=="nbeachb1";
}
inline bool VegetationModelName(const char* data,size_t limit=21) {
    if(!data||TerrainModelName(data,limit)) return false;
    std::string name;
    for(size_t i=0;i<limit&&data[i];++i) name.push_back(char(std::tolower(static_cast<unsigned char>(data[i]))));
    // Stock IDE models with neutral names but dedicated vegetation TXDs:
    // cl_vegbits (foliage around the club), man_veg_ext (hedge maze).
    // nbwbusshy2 is the stock North Beach bush group (misspelled "bush").
    for(const char* model:{"club_exterior08","club_exterior09","club_exterior11","club_exterior12","man_maze","nbwbusshy2"})
        if(name==model) return true;
    // Avoid unrelated street lights, garage doors, tree shadow decals, and
    // the ground slab dock_grassarea. Grass textures on roads aren't plants.
    for(const char* word:{"street","door","shadow","treeshad","infernus","grassarea"})
        if(name.find(word)!=std::string::npos) return false;
    for(const char* word:{"tree","palm","bush","plant","grass","veg","hedge","fern","flower","shrub","cactus","bamboo","plnt","ivy"})
        if(name.find(word)!=std::string::npos) return true;
    return false;
}
inline Scale VegetationScale(double phase,const Settings& settings,double height) {
    if(!std::isfinite(height)||height<=1e-5) return {1,1};
    auto visual=settings;visual.enabled=true;
    visual.amplitude*=std::clamp(settings.vegetationStrength,0.0,1.0);
    // Keep the requested percentage visible on tall plants; cap only extreme travel.
    visual.amplitude=std::min(visual.amplitude,std::clamp(settings.vegetationMaxDisplacement,0.0,2.0)/height);
    const auto scale=GetScale(phase,visual);
    return {1,scale.vertical}; // no spreading a plant group across walls
}
// RW modelling matrices are local to their parent. Convert the world-space
// deformation back to local space, including rotated/scaled parent frames.
template<class Matrix> bool VegetationLocalMatrix(const Matrix& world,const Matrix& parent,Matrix& local) {
    const auto& r=parent.right;const auto& u=parent.up;const auto& a=parent.at;
    const double cx=u.y*a.z-u.z*a.y,cy=u.z*a.x-u.x*a.z,cz=u.x*a.y-u.y*a.x;
    const double determinant=r.x*cx+r.y*cy+r.z*cz;
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-8) return false;
    auto inverse=[&](auto v){
        auto result=v;
        result.x=float((v.x*cx+v.y*cy+v.z*cz)/determinant);
        result.y=float((v.x*(a.y*r.z-a.z*r.y)+v.y*(a.z*r.x-a.x*r.z)+v.z*(a.x*r.y-a.y*r.x))/determinant);
        result.z=float((v.x*(r.y*u.z-r.z*u.y)+v.y*(r.z*u.x-r.x*u.z)+v.z*(r.x*u.y-r.y*u.x))/determinant);
        return result;
    };
    local=world;local.right=inverse(world.right);local.up=inverse(world.up);local.at=inverse(world.at);
    auto delta=world.pos;delta.x-=parent.pos.x;delta.y-=parent.pos.y;delta.z-=parent.pos.z;
    local.pos=inverse(delta);local.flags&=~(0x00020000u|0x00000003u);
    return true;
}
}
