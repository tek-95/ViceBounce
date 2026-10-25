#include "../src/MusicPlayback.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main(int argc,char** argv) {try {
    const bool supplied=argc>1;
    auto file=std::filesystem::absolute(supplied?argv[1]:"ViceBounceMusicTest.wav");
    // One second of original, silent PCM audio; no external track required.
    if(!supplied) {
    std::ofstream out(file,std::ios::binary);
    auto u16=[&](unsigned n){out.put(char(n));out.put(char(n>>8));};
    auto u32=[&](unsigned n){u16(n);u16(n>>16);};
    out.write("RIFF",4);u32(36+16000);out.write("WAVEfmt ",8);u32(16);
    u16(1);u16(1);u32(8000);u32(16000);u16(2);u16(16);
    out.write("data",4);u32(16000);
    for(int i=0;i<16000;++i) out.put(0);
    out.close();
    }
    vicebounce::MusicPlayback player;double position=0;
    Require(!player.Position(position),"disabled player started");
    player.Configure(true,file.wstring()+L".missing");
    Require(!player.Position(position)&&player.Error()!=0,"missing file not handled");
    player.Configure(true,file.wstring());
    if(!player.Position(position)) {
        char message[256]{};mciGetErrorStringA(player.Error(),message,256);
        std::cerr<<"MCI error "<<player.Error()<<": "<<message<<'\n';
        throw std::runtime_error("MCI audio open/play failed");
    }
    const auto initial=position;Sleep(180);
    Require(player.Position(position)&&position>initial+50,"audio position not advancing");
    if(supplied) {
        wchar_t length[64]{};
        Require(mciSendStringW(L"status ViceBounceMusic length",length,64,nullptr)==0,"MP3 duration query failed");
        const auto duration=std::wcstoul(length,nullptr,10);
        Require(duration>500,"MP3 too short");
        const auto seek=L"seek ViceBounceMusic to "+std::to_wstring(duration-250);
        Require(mciSendStringW(seek.c_str(),nullptr,0,nullptr)==0,"MP3 seek failed");
        Require(mciSendStringW(L"play ViceBounceMusic",nullptr,0,nullptr)==0,"MP3 resume failed");
        Require(player.Position(position),"MP3 near-end status failed");
    }
    player.SetPaused(true);
    Require(player.IsPaused(),"pause flag not set");
    Require(!player.Position(position),"paused player kept reporting a position");
    wchar_t pausedText[64]{};mciSendStringW(L"status ViceBounceMusic position",pausedText,64,nullptr);
    const auto pausedAt=std::wcstoul(pausedText,nullptr,10);
    Sleep(200);
    wchar_t pausedLater[64]{};mciSendStringW(L"status ViceBounceMusic position",pausedLater,64,nullptr);
    Require(std::wcstoul(pausedLater,nullptr,10)<=pausedAt+20,"paused audio position advanced");
    player.SetPaused(false);
    Require(!player.IsPaused(),"resume flag not cleared");
    Require(player.Position(position),"resume did not restore playback");
    bool looped=false;double previous=position;
    const auto deadline=GetTickCount64()+2500;
    while(GetTickCount64()<deadline) {
        Sleep(20);Require(player.Position(position),"audio status query failed");
        if(position+100<previous){looped=true;break;}
        previous=position;
    }
    Require(looped,"audio did not loop from end to beginning");
    player.Configure(false,L"");
    Require(!player.Position(position),"disabled playback kept running");
    if(!supplied) std::filesystem::remove(file);
    std::cout<<"PASS: local audio open, missing-file fallback, playback position, pause/resume, automatic loop and close\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
