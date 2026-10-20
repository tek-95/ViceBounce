#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <istream>
#include <string>
#include <cstdlib>
#include <cctype>

namespace sabounce {
class BeatMap {
    std::vector<double> beats;
public:
    bool Load(std::istream& stream) {
        beats.clear();std::string header;
        if(!std::getline(stream,header)) return false;
        if(!header.empty()&&header.back()=='\r') header.pop_back();
        if(header!="SABounceBeats 1") return false;
        std::string line;
        while(std::getline(stream,line)) {
            if(line.find_first_not_of(" \t\r")==std::string::npos) continue;
            if(line.size()>128) {beats.clear();return false;}
            char* end=nullptr;
            const double value=std::strtod(line.c_str(),&end);
            if(end==line.c_str()) {beats.clear();return false;}
            while(*end&&std::isspace(static_cast<unsigned char>(*end))) ++end;
            if(*end) {beats.clear();return false;}
            if(!std::isfinite(value)||value<0||value>43200000||beats.size()>=200000||
                (!beats.empty()&&value-beats.back()<80)) {beats.clear();return false;}
            beats.push_back(value);
        }
        if(stream.bad()||beats.size()<2) {beats.clear();return false;}
        return true;
    }
    bool Valid() const {return beats.size()>=2;}
    size_t Size() const {return beats.size();}
    double Phase(double position) const {
        if(!Valid()||!std::isfinite(position)) return .5;
        auto next=std::upper_bound(beats.begin(),beats.end(),position);
        size_t i=next==beats.begin()?0:next==beats.end()?beats.size()-2:size_t(next-beats.begin()-1);
        double phase=std::fmod((position-beats[i])/(beats[i+1]-beats[i])+.5,1.0);
        return phase<0?phase+1:phase;
    }
    double Nearest(double position) const {
        if(!Valid()||!std::isfinite(position)) return 0;
        auto next=std::lower_bound(beats.begin(),beats.end(),position);
        if(next==beats.begin()) return *next;
        if(next==beats.end()) return beats.back();
        return position-*(next-1)<*next-position?*(next-1):*next;
    }
};
}
