#pragma once
#include "Vegetation.h"
namespace vicebounce {
inline std::string SceneryName(const char* data,size_t limit=21) {
    std::string name;if(data) for(size_t i=0;i<limit&&data[i];++i) name.push_back(char(std::tolower(static_cast<unsigned char>(data[i]))));
    return name;
}
inline bool BuildingAttachmentName(const std::string& name) {
    for(const char* part:{"aircon","acunit","ladder","fireesc","balcony","awning","neon","hotel","od_wt","lighthouse"})
        if(name.find(part)!=std::string::npos) return true;
    return false;
}
inline bool BuildingDetailName(const std::string& name) {
    for(const char* part:{"aircon","acunit","ladder","fireesc","balcony","awning","neon","wtsign","wtlegs"})
        if(name.find(part)!=std::string::npos) return true;
    return name.find("hotel")!=std::string::npos&&name.find("sign")!=std::string::npos;
}
inline bool StreetObjectName(const std::string& name) {
    if(BuildingAttachmentName(name)) return false;
    for(const char* part:{"sign","traffic","lamp","lightpole","streetlight","bollard","barrier","bin","trash","rubbish","dumpster","bench","parkingmet","postbox","phone","hydrant","busstop","newsstand","parkmeter","fence","gate","railing"})
        if(name.find(part)!=std::string::npos) return true;
    return false;
}
inline bool BuildingCandidate(int entityType,bool vegetation,bool road,const char* data,size_t limit=21) {
    if(entityType!=1||vegetation||road||!data||TerrainModelName(data,limit)) return false;
    std::string name;
    for(size_t i=0;i<limit&&data[i];++i) name.push_back(char(std::tolower(static_cast<unsigned char>(data[i]))));
    if(StreetObjectName(name)) return false;
    if(name.find("water")!=std::string::npos&&name.find("tower")==std::string::npos&&name.find("tank")==std::string::npos) return false;
    for(const char* part:{"road","ground","terrain","pavement","sidewalk","seabed","shadow","treeshad","lightbeam","fence","billboard","bridge"})
        if(name.find(part)!=std::string::npos) return false;
    return !name.empty();
}
inline Scale BuildingScale(double phase,const Settings& settings,double height) {
    if(!std::isfinite(height)||height<.01) return {1,1};
    auto visual=settings;visual.enabled=true;
    visual.amplitude*=std::clamp(settings.buildingStrength,0.0,1.0);
    visual.amplitude=std::min(visual.amplitude,std::clamp(settings.buildingMaxDisplacement,0.0,10.0)/height);
    return {1,GetScale(phase,visual).vertical};
}
inline Scale StreetScale(double phase,const Settings& settings,double height) {
    if(!std::isfinite(height)||height<.01) return {1,1};
    auto visual=settings;visual.enabled=true;
    visual.amplitude*=settings.streetStrength;
    visual.amplitude=std::min(visual.amplitude,.75/height);
    return {1,GetScale(phase,visual).vertical};
}
}
