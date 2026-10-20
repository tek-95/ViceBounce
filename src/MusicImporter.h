#pragma once
#include <windows.h>
#include <string>
namespace sabounce {
class MusicImporter {
    HANDLE process=nullptr;
    std::wstring source;
    FILETIME modified{};
    DWORD sizeLow=0,sizeHigh=0;
    ULONGLONG nextCheck=0;
    bool automatic=true;
    void RememberSource() {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(GetFileAttributesExW(source.c_str(),GetFileExInfoStandard,&data)) {
            modified=data.ftLastWriteTime;sizeLow=data.nFileSizeLow;sizeHigh=data.nFileSizeHigh;
        }
    }
public:
    void Configure(const std::wstring& path,bool enabled) {
        source=path;automatic=enabled;RememberSource();nextCheck=GetTickCount64()+5000;
    }
    bool Running() const {return process!=nullptr;}
    bool Changed() {
        if(!automatic||source.empty()||Running()||GetTickCount64()<nextCheck) return false;
        nextCheck=GetTickCount64()+5000;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(!GetFileAttributesExW(source.c_str(),GetFileExInfoStandard,&data)) return false;
        return CompareFileTime(&modified,&data.ftLastWriteTime)!=0||sizeLow!=data.nFileSizeLow||sizeHigh!=data.nFileSizeHigh;
    }
    bool Start(const std::wstring& directory,bool picker=false) {
        if(Running()||(!picker&&(!automatic||source.empty()))) return false;
        const auto executable=directory+L"SABounceMusic.exe";
        if(GetFileAttributesW(executable.c_str())==INVALID_FILE_ATTRIBUTES) return false;
        // Windows filenames cannot contain quotes. All paths are quoted as
        // separate arguments; never pass a user filename through a shell.
        auto path=directory;
        while(!path.empty()&&(path.back()==L'\\'||path.back()==L'/')) path.pop_back();
        std::wstring command=L"\""+executable+L"\" --scripts-dir \""+path+L"\"";
        if(picker) {
            const auto window=GetForegroundWindow();
            DWORD owner=0;GetWindowThreadProcessId(window,&owner);
            if(owner==GetCurrentProcessId())
                command+=L" --game-window "+std::to_wstring(reinterpret_cast<uintptr_t>(window))+L" --game-pid "+std::to_wstring(owner);
        }
        if(!picker) command+=L" --quiet --if-changed --source \""+source+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=picker?SW_SHOWNORMAL:SW_HIDE;
        PROCESS_INFORMATION child{};
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&startup,&child)) return false;
        if(picker) AllowSetForegroundWindow(child.dwProcessId);
        CloseHandle(child.hThread);process=child.hProcess;RememberSource();return true;
    }
    // -1 still running/no process, 0 success, 2 user cancelled, 1 failure.
    int Poll() {
        if(!process) return -1;
        DWORD result=STILL_ACTIVE;
        if(!GetExitCodeProcess(process,&result)) result=1;
        if(result==STILL_ACTIVE) return -1;
        CloseHandle(process);process=nullptr;return int(result);
    }
};
}
