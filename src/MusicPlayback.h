#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <string>

namespace sabounce {
// Uses the player's position as the visual clock; no accumulating wall-clock drift.
class MusicPlayback {
    MCIDEVICEID device=0;
    std::wstring filename;
    bool requested=false,attempted=false;
    MCIERROR lastError=0;
public:
    void Configure(bool enabled,const std::wstring& path) {
        if(device) mciSendCommandW(device,MCI_CLOSE,MCI_WAIT,0);
        device=0;attempted=false;requested=enabled;filename=path;lastError=0;
    }
    bool Position(double& position) {
        if(!requested||filename.empty()) return false;
        if(!device&&!attempted) {
            attempted=true;
            if(GetFileAttributesW(filename.c_str())==INVALID_FILE_ATTRIBUTES) {
                lastError=MCIERR_FILE_NOT_FOUND;return false;
            }
            const auto extension=filename.substr(filename.find_last_of(L'.')+1);
            const wchar_t* type=(_wcsicmp(extension.c_str(),L"wav")==0)?L"waveaudio":L"mpegvideo";
            wchar_t shortPath[32768]{};
            const DWORD shortLength=GetShortPathNameW(filename.c_str(),shortPath,32768);
            const auto openPath=shortLength>0&&shortLength<32768?std::wstring(shortPath):filename;
            const std::wstring command=L"open \""+openPath+L"\" type "+type+L" alias SABounceMusic";
            lastError=mciSendStringW(command.c_str(),nullptr,0,nullptr);
            if(lastError) return false;
            device=mciGetDeviceIDW(L"SABounceMusic");
            MCI_SET_PARMS format{};format.dwTimeFormat=MCI_FORMAT_MILLISECONDS;
            lastError=mciSendCommandW(device,MCI_SET,MCI_SET_TIME_FORMAT|MCI_WAIT,reinterpret_cast<DWORD_PTR>(&format));
            if(lastError) {mciSendCommandW(device,MCI_CLOSE,MCI_WAIT,0);device=0;return false;}
            MCI_PLAY_PARMS play{};play.dwFrom=0;
            lastError=mciSendCommandW(device,MCI_PLAY,MCI_FROM,reinterpret_cast<DWORD_PTR>(&play));
        }
        if(!device||lastError) return false;
        MCI_STATUS_PARMS mode{};mode.dwItem=MCI_STATUS_MODE;
        lastError=mciSendCommandW(device,MCI_STATUS,MCI_STATUS_ITEM|MCI_WAIT,reinterpret_cast<DWORD_PTR>(&mode));
        if(lastError) return false;
        if(mode.dwReturn==MCI_MODE_STOP) {
            MCI_PLAY_PARMS play{};play.dwFrom=0;
            lastError=mciSendCommandW(device,MCI_PLAY,MCI_FROM,reinterpret_cast<DWORD_PTR>(&play));
            if(lastError) return false;
        }
        MCI_STATUS_PARMS status{};status.dwItem=MCI_STATUS_POSITION;
        lastError=mciSendCommandW(device,MCI_STATUS,MCI_STATUS_ITEM|MCI_WAIT,reinterpret_cast<DWORD_PTR>(&status));
        if(lastError) return false;
        position=double(status.dwReturn);return true;
    }
    MCIERROR Error() const {return lastError;}
};
}
