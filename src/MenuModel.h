#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

namespace vicebounce::panel {
enum class Page { Home, Settings, News };
enum class Action { None, Track, Vehicles, People, World, Strength, More,
    Release, Read, NewsLink, Cars, Other, Plants, Buildings, Street, Bpm,
    Style, PauseMusic, Reload, Back };
// News links are ordinary Rows: one type, colour and selection style for every action.
enum class Kind { Title, Heading, Text, Muted, Status, Rule, Row, Hint };
// One 4-unit rhythm for the native panel and exported browser preview.
inline constexpr float PanelWidth=218,PanelPadding=12,Space=8;
inline float FontSize(Kind kind){
    return kind==Kind::Title?.46f:kind==Kind::Heading?.20f:kind==Kind::Hint?.18f:.23f;
}
struct Data {
    std::string version="1.0.0",track="No track selected",playback="Stopped",latest,news,newsLink,newsLabel="Open link";
    bool cars=true,other=false,people=false,plants=false,buildings=false,street=false;
    bool paused=false,uk=false,beatMap=false;
    double strength=.1,bpm=135;
};
struct Row {
    Kind kind;std::string label,value;float y,height;
    Action action=Action::None;float x=0,width=218;
};
struct Frame {
    std::vector<Row> rows;float height=0;
    std::vector<Action> Actions()const{
        std::vector<Action> result;
        for(const auto& row:rows)if(row.action!=Action::None)result.push_back(row.action);
        return result;
    }
    void Add(Kind kind,std::string label={},std::string value={},float height=12,Action action=Action::None){
        rows.push_back({kind,label,value,this->height,height,action});this->height+=height;
    }
    void Rule(){Add(Kind::Rule,{},{},8);}
};
inline std::string Number(double value,int precision){char text[48];std::snprintf(text,sizeof(text),"%.*f",precision,value);return text;}
inline std::string On(bool value){return value?"On":"Off";}
inline std::string Group(bool a,bool b,bool c){return a&&b&&c?"On":!a&&!b&&!c?"Off":"Mixed";}
inline bool EnableGroup(bool a,bool b,bool c){return !(a&&b&&c);}
inline std::string TrackText(const std::wstring& name){
    std::string out;
    const std::wstring accents=L"\u00c0\u00c1\u00c2\u00c3\u00c4\u00c5\u00e0\u00e1\u00e2\u00e3\u00e4\u00e5\u00c7\u00e7\u00c8\u00c9\u00ca\u00cb\u00e8\u00e9\u00ea\u00eb\u00cc\u00cd\u00ce\u00cf\u00ec\u00ed\u00ee\u00ef\u00d1\u00f1\u00d2\u00d3\u00d4\u00d5\u00d6\u00d8\u00f2\u00f3\u00f4\u00f5\u00f6\u00f8\u00d9\u00da\u00db\u00dc\u00f9\u00fa\u00fb\u00fc\u00dd\u0178\u00fd\u00ff";
    const std::string plain="AAAAAAaaaaaaCcEEEEeeeeIIIIiiiiNnOOOOOOooooooUUUUuuuuYYyy";
    for(wchar_t ch:name){
        const auto index=accents.find(ch);
        out+=index!=std::wstring::npos?plain[index]:ch==0x2013||ch==0x2014?'-':ch==0x2019?'\'':ch>=32&&ch<=126&&ch!=L'~'?char(ch):'?';
    }
    return out;
}
inline std::vector<std::string> Wrap(std::string text,size_t width){
    std::vector<std::string> lines;
    while(!text.empty()){
        size_t end=text.size()>width?text.rfind(' ',width):text.size();
        if(end==std::string::npos||end==0)end=width;
        lines.push_back(text.substr(0,end));text.erase(0,end);
        while(!text.empty()&&text.front()==' ')text.erase(0,1);
    }
    return lines;
}
// Conservative proportional-font metrics in design units, shared by the
// preview and native layout. Wide words must not trigger CFont's own wrapping
// inside a separately positioned text row.
inline float TextWidth(const std::string& text){
    float width=0;
    for(char c:text){
        if(c==' ')width+=2.8f;
        else if(std::string("ilI.,:;!'|").find(c)!=std::string::npos)width+=2.8f;
        else if(std::string("MWmw@%").find(c)!=std::string::npos)width+=9.f;
        else if(c>='A'&&c<='Z')width+=6.8f;
        else width+=5.2f;
    }
    return width;
}
inline std::vector<std::string> WrapBody(std::string text,float width=212){
    std::vector<std::string> lines;
    while(!text.empty()){
        while(!text.empty()&&text.front()==' ')text.erase(0,1);
        if(text.empty())break;
        size_t end=0,space=std::string::npos;float used=0;
        while(end<text.size()&&used+TextWidth(text.substr(end,1))<=width){
            used+=TextWidth(text.substr(end,1));if(text[end]==' ')space=end;++end;
        }
        if(end<text.size()&&space!=std::string::npos&&space)end=space;
        if(!end)end=1;
        lines.push_back(text.substr(0,end));text.erase(0,end);
    }
    return lines;
}
inline std::string SummaryEnd(std::string text,float width=212){
    while(!text.empty()&&TextWidth(text+"...")>width)text.pop_back();
    while(!text.empty()&&text.back()==' ')text.pop_back();
    return text+"...";
}
// The track name gets the whole row left of the right-aligned "Change". A long
// name continues on a second line; only a name longer than two lines is shortened.
inline float TrackWidth(){return PanelWidth-TextWidth("Change")-Space;}
inline std::vector<std::string> TrackLines(const std::string& track){
    auto lines=WrapBody(track,TrackWidth());
    if(lines.empty())lines.push_back(track);
    if(lines.size()>2){lines.resize(2);lines[1]=SummaryEnd(lines[1],TrackWidth());}
    return lines;
}
inline Frame Build(Page page,const Data& data){
    Frame f;f.Add(Kind::Title,"VICEBOUNCE",data.version,20);f.Rule();
    if(page==Page::Settings){
        f.Add(Kind::Heading,"MORE SETTINGS",{},16);
        f.Add(Kind::Row,"Cars + cutscenes",On(data.cars),12,Action::Cars);
        f.Add(Kind::Row,"Bikes, boats, aircraft",On(data.other),12,Action::Other);
        f.Add(Kind::Row,"Vegetation",On(data.plants),12,Action::Plants);
        f.Add(Kind::Row,"Buildings",On(data.buildings),12,Action::Buildings);
        f.Add(Kind::Row,"Street objects",On(data.street),12,Action::Street);
        f.Rule();
        f.Add(Kind::Row,"Tempo",Number(data.bpm,0)+" BPM",12,Action::Bpm);
        f.Add(Kind::Row,"Bounce style",data.uk?"UK":"Russian",12,Action::Style);
        f.Add(Kind::Row,"Music",data.paused?"Paused":"Playing",12,Action::PauseMusic);
        f.Rule();
        f.Add(Kind::Row,"Reload settings",{},12,Action::Reload);
        f.Add(Kind::Row,"Back",{},12,Action::Back);
    }else if(page==Page::News){
        f.Add(Kind::Heading,"FROM THE DEVELOPER",{},16);
        for(const auto& text:WrapBody(data.news.empty()?"No new announcement.":data.news))f.Add(Kind::Text,text,{},16);
        f.height+=Space;
        if(!data.news.empty()&&!data.newsLink.empty())f.Add(Kind::Row,data.newsLabel,{},12,Action::NewsLink);
        f.Add(Kind::Row,"Back",{},12,Action::Back);f.height+=4;
    }else{
        const auto track=TrackLines(data.track);
        std::string label=track[0];
        if(track.size()>1)label+="\n"+track[1]; // the renderer prints one 12-unit line per '\n'
        f.Add(Kind::Row,label,"Change",12.f*track.size(),Action::Track);
        f.Add(Kind::Status,Number(data.bpm,0)+" BPM",data.playback,12);
        f.Rule();f.Add(Kind::Heading,"BOUNCE",{},12);
        f.Add(Kind::Row,"Vehicles",Group(data.cars,data.other,data.other),12,Action::Vehicles);
        f.Add(Kind::Row,"People",On(data.people),12,Action::People);
        f.Add(Kind::Row,"World",Group(data.plants,data.buildings,data.street),12,Action::World);
        f.Add(Kind::Row,"Strength",Number(data.strength,2),12,Action::Strength);
        // Every selectable row is 12 units, so the pink selection fits its text; group
        // spacing is added below the row instead of making the row taller.
        f.Add(Kind::Row,"More settings",{},12,Action::More);f.height+=4;
        if(!data.latest.empty()){
            f.Rule();f.Add(Kind::Row,data.latest+" available","View release",12,Action::Release);f.height+=4;
        }
        if(!data.news.empty()){
            f.Rule();f.Add(Kind::Heading,"FROM THE DEVELOPER",{},16);
            const auto lines=WrapBody(data.news);
            for(size_t i=0;i<(std::min)(lines.size(),size_t(2));++i)
                f.Add(Kind::Text,i==1&&lines.size()>2?SummaryEnd(lines[i]):lines[i],{},16);
            const bool read=lines.size()>2,link=!data.newsLink.empty();
            if(read||link){
                f.height+=4;
                if(link)f.Add(Kind::Row,data.newsLabel,{},12,Action::NewsLink);
                if(read)f.Add(Kind::Row,"Read more",{},12,Action::Read);
                f.height+=4;
            }
        }
    }
    f.Rule();f.Add(Kind::Hint,"Arrows Navigate   Enter Select   F11 Close",{},12);
    return f;
}
inline Action Normalize(Action selected,const Frame& frame){
    const auto actions=frame.Actions();
    return std::find(actions.begin(),actions.end(),selected)!=actions.end()?selected:actions.front();
}
inline Action Move(Action selected,int direction,const Frame& frame){
    const auto actions=frame.Actions();const auto found=std::find(actions.begin(),actions.end(),selected);
    const int index=found==actions.end()?0:int(found-actions.begin()),count=int(actions.size());
    return actions[(index+direction+count)%count];
}
inline bool Adjustable(Action action){
    switch(action){case Action::Vehicles:case Action::People:case Action::World:case Action::Strength:
    case Action::Cars:case Action::Other:case Action::Plants:case Action::Buildings:case Action::Street:
    case Action::Bpm:case Action::Style:case Action::PauseMusic:return true;
    default:return false;}
}
}
