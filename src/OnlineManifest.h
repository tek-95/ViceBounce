#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <sstream>

namespace vicebounce {
inline constexpr char CurrentVersion[]="1.0.0";
inline constexpr wchar_t ReleasesUrl[]=L"https://github.com/tek-95/ViceBounce/releases";
inline bool AnnouncementUrl(const std::string& url){
    if(url.empty())return true;
    if(url.size()>1024||url.compare(0,8,"https://")!=0)return false;
    auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    for(size_t i=0;i<url.size();++i){
        const unsigned char c=url[i];
        if(c<=32||c>=127||c=='\\'||c=='"'||c=='<'||c=='>'||c=='`')return false;
        if(c=='%'){
            if(i+2>=url.size()||hex(url[i+1])<0||hex(url[i+2])<0)return false;
            const int decoded=hex(url[i+1])*16+hex(url[i+2]);
            if(decoded<32||decoded==127||decoded=='\\')return false;i+=2;
        }
    }
    const auto end=url.find_first_of("/?#",8);
    std::string host=url.substr(8,end==std::string::npos?end:end-8);
    const auto colon=host.find(':');
    if(colon!=std::string::npos){
        const auto port=host.substr(colon+1);unsigned number=0;
        if(port.empty()||port.size()>5)return false;
        for(char c:port){if(c<'0'||c>'9')return false;number=number*10+c-'0';}
        if(!number||number>65535)return false;host.resize(colon);
    }
    if(host.empty()||host.size()>253)return false;
    size_t label=0;
    for(size_t i=0;i<=host.size();++i){
        if(i==host.size()||host[i]=='.'){
            if(!label||label>63||host[i-label]=='-'||host[i-1]=='-')return false;label=0;
        }else{
            const char c=host[i];
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return false;++label;
        }
    }
    return true;
}
struct ReleaseVersion {
    std::array<unsigned,3> parts{};
    static bool Parse(const std::string& text,ReleaseVersion& out) {
        if(text.empty()||text.size()>32)return false;
        ReleaseVersion result;size_t i=text[0]=='v'?1:0;
        for(int part=0;part<3;++part){
            const size_t begin=i;unsigned value=0;
            while(i<text.size()&&text[i]>='0'&&text[i]<='9'){
                if(i-begin>=6)return false;
                value=value*10+unsigned(text[i++]-'0');
            }
            if(i==begin||(i-begin>1&&text[begin]=='0'))return false;
            result.parts[part]=value;
            if(part<2){if(i>=text.size()||text[i++]!='.')return false;}
        }
        if(i!=text.size())return false;out=result;return true;
    }
};
struct OnlineManifest {
    std::string latest,id,message,url,label;
    ReleaseVersion version;
    static bool Parse(const std::string& body,OnlineManifest& out){
        if(body.empty()||body.size()>4096)return false;
        OnlineManifest result;unsigned seen=0;std::istringstream input(body);std::string line;
        while(std::getline(input,line)){
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.empty()||line[0]=='#')continue;
            for(unsigned char c:line)if(c<32||c>126)return false;
            const auto split=line.find('=');if(split==std::string::npos)return false;
            const auto key=line.substr(0,split),value=line.substr(split+1);unsigned bit=0;
            if(key=="schema"){bit=1;if(value!="1")return false;}
            else if(key=="latest"){bit=2;result.latest=value;}
            else if(key=="announcement_id"){bit=4;result.id=value;}
            else if(key=="message"){bit=8;result.message=value;}
            else if(key=="announcement_url"){bit=16;result.url=value;}
            else if(key=="announcement_label"){bit=32;result.label=value;}
            else return false;
            if(seen&bit)return false;seen|=bit;
        }
        if((seen&15)!=15||!ReleaseVersion::Parse(result.latest,result.version)||result.id.size()>40||result.message.size()>160||result.label.size()>20)return false;
        if(result.message.find('~')!=std::string::npos||result.label.find('~')!=std::string::npos||!AnnouncementUrl(result.url))return false;
        if((!result.url.empty()&&result.message.empty())||(!result.label.empty()&&result.url.empty()))return false;
        for(unsigned char c:result.id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
        if(!result.message.empty()&&result.id.empty())return false;
        out=result;return true;
    }
    bool NewerThanCurrent() const {ReleaseVersion current;ReleaseVersion::Parse(CurrentVersion,current);return current.parts<version.parts;}
};
struct OnlineCache {
    uint64_t attempt=0,success=0;std::string body;
    static bool Parse(const std::string& raw,OnlineCache& out){
        if(raw.size()>4224)return false;
        std::istringstream input(raw);std::string magic,a,b;
        if(!std::getline(input,magic)||magic!="VBCACHE1"||!std::getline(input,a)||!std::getline(input,b))return false;
        auto number=[](const std::string& s,uint64_t& n){
            if(s.empty()||s.size()>12)return false;n=0;
            for(char c:s){if(c<'0'||c>'9')return false;n=n*10+uint64_t(c-'0');}return true;
        };
        OnlineCache result;if(!number(a,result.attempt)||!number(b,result.success)||result.success>result.attempt)return false;
        result.body.assign(std::istreambuf_iterator<char>(input),{});
        OnlineManifest manifest;if(!result.body.empty()&&!OnlineManifest::Parse(result.body,manifest))return false;
        if(result.body.empty()&&result.success)return false;
        out=result;return true;
    }
    std::string Encode() const{return "VBCACHE1\n"+std::to_string(attempt)+"\n"+std::to_string(success)+"\n"+body;}
    uint64_t Delay(uint64_t now,unsigned interval) const {
        // A moved-back system clock must not silence updates indefinitely.
        if(attempt>now||!attempt||now-attempt>=interval)return 0;
        return interval-(now-attempt);
    }
};
inline bool AcceptOnlineResponse(OnlineCache& cache,uint64_t now,bool received,const std::string& body,OnlineManifest& manifest){
    if(cache.success>now)cache.success=0;
    cache.attempt=now;
    OnlineManifest candidate;
    if(!received||!OnlineManifest::Parse(body,candidate))return false;
    cache.success=now;cache.body=body;manifest=candidate;return true;
}
}
