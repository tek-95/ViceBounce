#pragma once
#include <vector>
#include <numeric>
#include <limits>
#include <cmath>
#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <cstring>
namespace vicebounce {
struct SceneryBounds {
    float x0=INFINITY,y0=INFINITY,z0=INFINITY,x1=-INFINITY,y1=-INFINITY,z1=-INFINITY;
    template<class V> void Add(const V& p) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) return;
        x0=std::min(x0,p.x);y0=std::min(y0,p.y);z0=std::min(z0,p.z);
        x1=std::max(x1,p.x);y1=std::max(y1,p.y);z1=std::max(z1,p.z);
    }
    bool Valid() const {return std::isfinite(z0)&&std::isfinite(z1);}
    void Merge(const SceneryBounds& b) {x0=std::min(x0,b.x0);y0=std::min(y0,b.y0);z0=std::min(z0,b.z0);x1=std::max(x1,b.x1);y1=std::max(y1,b.y1);z1=std::max(z1,b.z1);}
    float Distance(const SceneryBounds& b) const {
        const float dx=std::max({0.f,x0-b.x1,b.x0-x1}),dy=std::max({0.f,y0-b.y1,b.y0-y1}),dz=std::max({0.f,z0-b.z1,b.z0-z1});
        return dx*dx+dy*dy+dz*dz;
    }
};
struct SceneryProbe {float x,y;};
inline std::array<SceneryProbe,29> SceneryGroundProbes(const SceneryBounds& b) {
    const float cx=(b.x0+b.x1)*.5f,cy=(b.y0+b.y1)*.5f;
    const float left=b.x0-.75f,right=b.x1+.75f,bottom=b.y0-.75f,top=b.y1+.75f;
    // Corners alone miss the road on long blocks and often fall into water
    // around the stadium. Probe edge midpoints and inner courtyards as well.
    std::array<SceneryProbe,29> result={{{cx,cy},{left,bottom},{right,bottom},{left,top},{right,top},
             {cx,bottom},{cx,top},{left,cy},{right,cy},
             {(b.x0+cx)*.5f,(b.y0+cy)*.5f},{(b.x1+cx)*.5f,(b.y0+cy)*.5f},
             {(b.x0+cx)*.5f,(b.y1+cy)*.5f},{(b.x1+cx)*.5f,(b.y1+cy)*.5f}}};
    size_t i=13;
    // Fallback for narrow pavement strips along a large coastal footprint.
    for(float t:{.125f,.375f,.625f,.875f}) {
        const float x=b.x0+(b.x1-b.x0)*t,y=b.y0+(b.y1-b.y0)*t;
        result[i++]={left,y};result[i++]={right,y};
        result[i++]={x,bottom};result[i++]={x,top};
    }
    return result;
}
// Only substantial, non-LOD bodies join groups. Details/LODs then inherit one
// body's bounds without bridging independent buildings through large LODs.
inline std::vector<SceneryBounds> GroupScenery(const std::vector<SceneryBounds>& parts,const std::vector<bool>& core,const std::vector<bool>& expand={}) {
    const size_t n=parts.size();std::vector<size_t> owner(n);std::iota(owner.begin(),owner.end(),0);
    auto root=[&](size_t a){while(owner[a]!=a) a=owner[a];return a;};
    for(size_t i=0;i<n;++i) if(core[i]) for(size_t j=0;j<i;++j)
        if(core[j]&&parts[i].Distance(parts[j])<=.75f*.75f) owner[root(i)]=root(j);
    auto groups=parts;
    for(size_t i=0;i<n;++i) if(core[i]) groups[root(i)].Merge(parts[i]);
    std::vector<size_t> host(n,n);
    for(size_t i=0;i<n;++i) {
        if(core[i]) {host[i]=root(i);continue;}
        float best=INFINITY;size_t target=n;
        for(size_t j=0;j<n;++j) if(core[j]&&root(j)==j&&parts[i].Distance(groups[j])<=2.5f*2.5f) {
            const auto& b=groups[j];
            const float dx=(parts[i].x0+parts[i].x1-b.x0-b.x1)*.5f,dy=(parts[i].y0+parts[i].y1-b.y0-b.y1)*.5f;
            const float score=parts[i].Distance(b)*10000+dx*dx+dy*dy;
            if(score<best){best=score;target=j;}
        }
        host[i]=target;
    }
    // Legs can extend below a tank's own mesh: include real details in the
    // common base/height. A large distant LOD must never enlarge that group.
    for(size_t i=0;i<n;++i) if(!core[i]&&host[i]<n&&(expand.empty()||expand[i])) groups[host[i]].Merge(parts[i]);
    auto result=parts;
    for(size_t i=0;i<n;++i) if(host[i]<n) result[i]=groups[host[i]];
    return result;
}
struct ScenerySupport {float z;bool found;};
template<class Probe> std::vector<ScenerySupport> SharedSceneryGround(const std::vector<SceneryBounds>& parts,
        const std::vector<SceneryBounds>& groups,const std::vector<bool>& core,Probe probe) {
    std::unordered_map<std::string,ScenerySupport> cache;
    std::vector<ScenerySupport> result;
    for(const auto& group:groups) {
        const std::string key(reinterpret_cast<const char*>(&group),sizeof(group));
        auto found=cache.find(key);
        if(found==cache.end()) {
            ScenerySupport support{group.z0,false};support.found=probe(group,support.z);
            if(!support.found) {
                std::vector<float> candidates;
                for(size_t j=0;j<parts.size();++j) if(core[j]&&std::memcmp(&groups[j],&group,sizeof(group))==0) {
                    float z=parts[j].z0;
                    if(probe(parts[j],z)) candidates.push_back(z);
                }
                if(!candidates.empty()) {
                    std::sort(candidates.begin(),candidates.end());
                    support={candidates[candidates.size()/2],true};
                }
            }
            found=cache.emplace(key,support).first;
        }
        result.push_back(found->second);
    }
    return result;
}
}
