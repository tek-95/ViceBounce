#include "../src/OnlineUpdates.h"
#include "../src/MenuModel.h"
#include "../src/MenuFontLayout.h"
#define main VerifyExeProgramMain
#include "../tools/VerifyExe.cpp"
#undef main
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
std::string Body(const std::string& version="1.0.1",const std::string& id="notice-1",const std::string& message="A short developer message."){
    return "schema=1\nlatest="+version+"\nannouncement_id="+id+"\nmessage="+message+"\n";
}
void LogicTests(){
    vicebounce::ReleaseVersion version;
    for(const auto* good:{"1.0.0","0.19.0","v1.0.1","10.2.100"})Check(vicebounce::ReleaseVersion::Parse(good,version),"valid stable version");
    for(const auto* bad:{"","1.0","1.0.0.1","1.0.0-beta","01.0.0","-1.0.0","1.0.-1","1.0.0 ","1.0.1000000","1.0.0\n"})
        Check(!vicebounce::ReleaseVersion::Parse(bad,version),"invalid version rejected");
    vicebounce::OnlineManifest manifest;
    Check(vicebounce::OnlineManifest::Parse(Body(),manifest)&&manifest.NewerThanCurrent(),"newer release detected");
    Check(vicebounce::OnlineManifest::Parse(Body("1.0.0"),manifest)&&!manifest.NewerThanCurrent(),"same release is not an update");
    Check(vicebounce::OnlineManifest::Parse(Body("0.19.9"),manifest)&&!manifest.NewerThanCurrent(),"older release is not an update");
    Check(vicebounce::OnlineManifest::Parse(Body("1.0.10"),manifest)&&manifest.NewerThanCurrent(),"numeric rather than lexical comparison");
    Check(vicebounce::OnlineManifest::Parse(Body("1.0.0","",""),manifest),"announcement can be removed");
    for(const auto& bad:{Body()+"latest=9.0.0\n",Body()+"url=https://evil.example/\n",Body("1.0.1","","Text"),
        Body("1.0.1","bad/id"),Body("1.0.1","notice","~r~red text"),Body("1.0.1","notice",std::string(161,'x')),
        std::string(4097,'a'),std::string("schema=2\nlatest=1.0.1\nannouncement_id=x\nmessage=x\n"),
        Body("1.0.1","notice","escape\ttext"),Body("1.0.1","notice",std::string("bad\0text",8))})
        Check(!vicebounce::OnlineManifest::Parse(bad,manifest),"malformed or executable-like metadata rejected");
    Check(vicebounce::OnlineManifest::Parse("# header\r\nschema=1\r\nlatest=1.0.1\r\nannouncement_id=x\r\nmessage=News\r\n",manifest),"CRLF and comments supported");
    for(const auto* url:{"https://github.com/tek-95/ViceBounce","https://t.me/tek95","https://example.com:443/a?x=1&y=2#news","https://example.com/~creator/a%20b"}){
        Check(vicebounce::OnlineManifest::Parse(Body()+"announcement_url="+url+"\nannouncement_label=Learn more\n",manifest)&&manifest.url==url&&manifest.label=="Learn more","optional announcement link parses and retains query/label");
    }
    for(const auto* url:{"http://example.com/","file:///C:/test.exe","javascript:alert(1)","steam://run/123","https://","https://bad host/a","https://user@example.com/a","https://-bad.example/a","https://example..com/","https://example.com\\a","https://example.com/%0a","https://example.com/%5c","https://example.com/a%xx","https://example.com:99999/","https://example.com/\"x"})
        Check(!vicebounce::OnlineManifest::Parse(Body()+"announcement_url="+url+"\n",manifest),"invalid or non-web announcement targets are rejected");
    Check(!vicebounce::OnlineManifest::Parse(Body()+"announcement_label=Learn more\n",manifest),"button label requires a link");
    Check(!vicebounce::OnlineManifest::Parse(Body("1.0.0","","")+"announcement_url=https://example.com/\n",manifest),"link requires announcement content");
    Check(!vicebounce::OnlineManifest::Parse(Body()+"announcement_url=https://example.com/\nannouncement_url=https://example.org/\n",manifest),"duplicate links are rejected");
    Check(!vicebounce::OnlineManifest::Parse(Body()+"announcement_url=https://example.com/\nannouncement_label=~r~News\n",manifest),"button label cannot introduce game font tokens");
    vicebounce::OnlineCache cache{1000,900,Body()},decoded;
    Check(vicebounce::OnlineCache::Parse(cache.Encode(),decoded)&&decoded.body==cache.body,"cache round trip");
    Check(decoded.Delay(1100,86400)==86300,"daily cache avoids repeated requests");
    Check(decoded.Delay(900,86400)==0,"backwards clock retries instead of hiding updates");
    Check(decoded.Delay(90000,86400)==0,"old attempt expires");
    Check(!vicebounce::OnlineCache::Parse("VBCACHE1\n100\n101\n"+Body(),decoded),"impossible cache timestamps rejected");
    Check(!vicebounce::OnlineCache::Parse("VBCACHE1\n100\n90\ngarbage",decoded),"corrupt cached payload rejected");
    Check(!vicebounce::OnlineCache::Parse("VBCACHE1\n9999999999999999999999999999\n0\n",decoded),"oversize cache number rejected");
    const auto oldBody=cache.body;
    Check(!vicebounce::AcceptOnlineResponse(cache,2000,false,"",manifest)&&cache.body==oldBody&&cache.success==900&&cache.attempt==2000,"offline response keeps last good message and records retry time");
    Check(!vicebounce::AcceptOnlineResponse(cache,3000,true,"schema=999\n",manifest)&&cache.body==oldBody,"bad HTTP 200 payload cannot replace good cache");
    Check(vicebounce::AcceptOnlineResponse(cache,4000,true,Body("1.2.0"),manifest)&&manifest.latest=="1.2.0"&&cache.success==4000,"valid response updates cache and release together");
    Check(vicebounce::OnlineCache::Parse(cache.Encode(),decoded),"refreshed cache remains readable");
    uint32_t random=12;
    for(int sample=0;sample<2000;++sample){std::string garbage;for(int i=0;i<sample%200;++i){random=random*1664525u+1013904223u;garbage+=char(random>>24);}
        Check(!vicebounce::OnlineManifest::Parse(garbage,manifest),"arbitrary bytes fail closed");}
}
void CacheWorkerTest(){
    const auto directory=std::filesystem::current_path()/"online-test-cache";
    std::filesystem::create_directories(directory);
    const auto path=directory/L"ViceBounceUpdate.cache";
    const auto now=uint64_t(std::time(nullptr));
    {std::ofstream out(path,std::ios::binary);out<<vicebounce::OnlineCache{now,now,Body()}.Encode();}
    vicebounce::OnlineUpdates updates;updates.Configure(directory.wstring(),true,24);
    const auto start=GetTickCount64();updates.OnMenu();
    Check(GetTickCount64()-start<100,"menu call does not wait for metadata IO");
    vicebounce::OnlineSnapshot snapshot;
    for(int i=0;i<200;++i){snapshot=updates.Snapshot();if(snapshot.valid)break;Sleep(5);}
    Check(snapshot.valid&&snapshot.manifest.latest=="1.0.1","background worker reads fresh cache without network");
    Check(snapshot.error.empty(),"fresh cache is not a network failure");
    updates.Configure(directory.wstring(),false,24);updates.OnMenu();
    // Wait for the bounded cache-only worker before cleaning our own file.
    Sleep(20);std::filesystem::remove(path);std::filesystem::remove(directory);
}
void MenuTests(){
    using namespace vicebounce::panel;
    Data data;const auto quiet=Build(Page::Home,data);
    auto has=[](const Frame& frame,Action action){const auto actions=frame.Actions();return std::find(actions.begin(),actions.end(),action)!=actions.end();};
    Check(!has(quiet,Action::Release)&&!has(quiet,Action::Read)&&!has(quiet,Action::NewsLink),"empty online sections do not occupy the home menu");
    data.latest="1.0.1";auto update=Build(Page::Home,data);
    Check(has(update,Action::Release)&&!has(update,Action::NewsLink),"release notice is independent from news");
    data.latest.clear();data.news=std::string(160,'x');auto news=Build(Page::Home,data);
    Check(!has(news,Action::Release)&&has(news,Action::Read)&&!has(news,Action::NewsLink),"news actions do not require a release");
    data.newsLink="https://github.com/tek-95/ViceBounce";data.newsLabel="Visit project";
    Check(has(Build(Page::Home,data),Action::NewsLink)&&has(Build(Page::News,data),Action::NewsLink),"announcement link is available on home and full text pages");
    data.latest="123456.123456.123456";data.track=std::string(200,'x');
    for(auto page:{Page::Home,Page::Settings,Page::News}){
        const auto frame=Build(page,data);const auto actions=frame.Actions();
        Check(frame.height<290,"panel fits below HUD with worst-case metadata");
        float previous=-1;
        for(const auto& row:frame.rows){
            Check(row.y>=previous&&row.y+row.height<=frame.height,"rows stay ordered and inside the panel");previous=row.y;
            Check(row.x>=0&&row.x+row.width<=218,"inline actions stay inside the panel width");
            if(row.action!=Action::None&&row.action!=Action::Track)
                Check(row.kind==Kind::Row&&row.height==12,"every selectable row has the same 12-unit row and selection");
        }
        auto selected=actions.front();
        for(size_t i=0;i<actions.size();++i)selected=Move(selected,1,frame);
        Check(selected==actions.front(),"keyboard navigation wraps once through visible actions");
        Check(Move(actions.front(),-1,frame)==actions.back(),"reverse navigation wraps");
    }
    Check(Normalize(Action::NewsLink,quiet)==Action::Track,"removed announcement links cannot leave focus on a hidden action");
    Check(Group(true,false,true)=="Mixed"&&EnableGroup(true,false,true),"partially enabled groups report Mixed and toggle fully on");
    Check(!EnableGroup(true,true,true)&&EnableGroup(false,false,false),"group toggles fully off or on");
    Check(!Adjustable(Action::Track)&&!Adjustable(Action::Release)&&Adjustable(Action::Strength),"left/right never opens picker or browser");
    const auto settings=Build(Page::Settings,data);
    for(auto action:{Action::Cars,Action::Other,Action::Plants,Action::Buildings,Action::Street,Action::Bpm,Action::Style,Action::PauseMusic,Action::Reload,Action::Back})
        Check(has(settings,action),"detailed controls remain available");
    Check(quiet.height<news.height,"panel shrinks when news is absent");
    Check(TrackText(L"Patrick S\u00e9bastien - Toc toc")=="Patrick Sebastien - Toc toc","Latin track names retain readable letters in the game font");
    Check(TrackText(L"~r~\u2019\u2013")=="?r?'-","track names cannot introduce font tokens");
    data.news="Short announcement.";data.newsLink="https://example.com/";
    const auto shortNews=Build(Page::Home,data);
    Check(!has(shortNews,Action::Read)&&has(shortNews,Action::NewsLink),"short complete announcements do not need a duplicate read action");
    for(const auto& row:shortNews.rows)if(row.action==Action::NewsLink)
        Check(row.kind==Kind::Row&&row.x==0&&row.width==PanelWidth,"announcement link is an ordinary full-width menu row");
    // Track names use the measured row width and a second line before shortening.
    auto trackRow=[&](const std::string& name){data.track=name;for(const auto& row:Build(Page::Home,data).rows)if(row.action==Action::Track)return row;return Row{};};
    const std::string medium="Patrick Sebastien - Toc toc toc (Club Mix)";
    Check(trackRow(medium).label.find("...")==std::string::npos,"a 42-character track name is not shortened");
    Check(trackRow("Short").height==12&&trackRow(medium).height==24,"a long track name takes a second 12-unit line");
    const auto longest=trackRow(std::string(200,'x'));
    Check(longest.height==24&&longest.label.size()>=3&&longest.label.compare(longest.label.size()-3,3,"...")==0,"only names longer than two lines are shortened");
    for(const auto& name:{medium,std::string(200,'W'),std::string(200,'x')}){
        const auto label=trackRow(name).label;size_t start=0;
        for(size_t end;start<=label.size();start=end+1){
            end=label.find('\n',start);if(end==std::string::npos)end=label.size();
            Check(TextWidth(label.substr(start,end-start))<=TrackWidth()+.01f,"track lines never reach the right-aligned Change");
        }
    }
    for(char glyph:{'W','i','x'}){
        data.news=std::string(160,glyph);data.newsLabel=std::string(20,'W');
        for(auto page:{Page::Home,Page::News}){
            const auto frame=Build(page,data);Check(frame.height<290,"wide maximum-length news still fits below HUD");
            for(const auto& row:frame.rows){
                Check(std::fmod(row.y,4.f)==0&&std::fmod(row.height,4.f)==0,"menu rows use one four-unit spacing rhythm");
                if(row.kind==Kind::Text)Check(TextWidth(row.label)<=212.01f,"proportional news text stays inside its column");
                if(row.action==Action::NewsLink||row.action==Action::Read){
                    Check(row.kind==Kind::Row&&TextWidth(row.label)<=PanelWidth,"long custom link labels stay inside the panel");
                    for(const auto& other:frame.rows)if(other.kind==Kind::Text&&other.y<row.y)
                        Check(row.y-(other.y+other.height)>=3.99f,"news actions have a visible gap below the paragraph");
                }
            }
        }
    }
}
void FontTests(const char* path){
    std::ifstream in(std::filesystem::u8path(path),std::ios::binary);Check(bool(in),"font sample opens");
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),{});ImageMemory image(bytes);
    int matches=0;
    for(auto exe:{vicebounce::GameExe::Retail10,vicebounce::GameExe::Retail11,vicebounce::GameExe::Steam}){
        if(!vicebounce::VerifyMenuFont(exe,image))continue;++matches;
        const auto font=vicebounce::MenuFontFor(exe);
        image.image[font.print-image.base]^=1;
        Check(!vicebounce::VerifyMenuFont(exe,image),"changed font function disables menu overlay");
        image.image[font.print-image.base]^=1;
        // The skewed panel frame (DrawAnyRect) must verify on every local sample.
        Check(vicebounce::VerifyMenuQuad(exe,image),"DrawAnyRect matches for the skewed menu frame");
        image.image[font.quad+0x20-image.base]^=1;
        Check(!vicebounce::VerifyMenuQuad(exe,image),"changed DrawAnyRect keeps the rectangular panel");
        image.image[font.quad+0x20-image.base]^=1;
        std::cout<<"Verified menu font: "<<path<<" details="<<std::hex<<font.details<<" quad="<<font.quad<<std::dec<<"\n";
    }
    Check(matches==1,"exactly one version-specific menu renderer matches");
}
}
int main(int argc,char** argv){
    try{LogicTests();MenuTests();CacheWorkerTest();for(int i=1;i<argc;++i)FontTests(argv[i]);
        std::cout<<"ONLINE CHECKS PASSED: "<<checks<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"ONLINE CHECK FAILED: "<<e.what()<<"\n";return 1;}
}
