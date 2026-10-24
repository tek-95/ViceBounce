#pragma once
// Optional crash and hang recorder for finding out why the game fails on someone's machine.
//
// It is compiled in only when VICEBOUNCE_DIAGNOSTICS is defined, so the normal build is unchanged.
// When enabled it appends to ViceBounce.log:
//   * an EXCEPTION block for serious CPU exceptions (access violation, illegal instruction, ...): the code,
//     where it happened (module + offset), the registers, the plugin entry point that ran last, and the
//     return addresses it can recognise on the stack;
//   * a WATCHDOG line if no frame is drawn for several seconds (a hang rather than a crash);
//   * the list of loaded modules, so other ASI plugins can be told apart.
// Everything that runs while handling an exception avoids the heap and the C++ runtime on purpose.
#ifdef VICEBOUNCE_DIAGNOSTICS
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace vicebounce::diag {

inline const char* volatile stage="startup";
inline volatile unsigned long frames=0;
inline char path[MAX_PATH]{};

inline void Raw(const char* text) {
    const HANDLE file=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    DWORD written=0;
    WriteFile(file,text,static_cast<DWORD>(std::strlen(text)),&written,nullptr);
    CloseHandle(file);
}
// "module.dll+0x1234" for an address inside a loaded module, otherwise just the address.
inline void Describe(char* out,size_t size,uintptr_t address) {
    HMODULE module=nullptr;
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(address),&module)&&module) {
        char file[MAX_PATH]{};
        GetModuleFileNameA(module,file,MAX_PATH);
        const char* name=std::strrchr(file,'\\');
        std::snprintf(out,size,"%s+0x%X",name?name+1:file,unsigned(address-reinterpret_cast<uintptr_t>(module)));
    } else std::snprintf(out,size,"unmapped");
}
inline bool Readable(const void* address,size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    for(const auto* p=static_cast<const unsigned char*>(address);p<static_cast<const unsigned char*>(address)+size;) {
        if(!VirtualQuery(p,&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
        p=static_cast<const unsigned char*>(info.BaseAddress)+info.RegionSize;
    }
    return true;
}
inline bool LooksLikeReturnAddress(uintptr_t value) {
    MEMORY_BASIC_INFORMATION info{};
    if(value<0x10000||!VirtualQuery(reinterpret_cast<void*>(value),&info,sizeof(info))) return false;
    if(info.State!=MEM_COMMIT||info.Type!=MEM_IMAGE) return false;
    if(!(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
    if(!Readable(reinterpret_cast<void*>(value-6),6)) return false;
    const auto* code=reinterpret_cast<const unsigned char*>(value);
    return code[-5]==0xE8||code[-6]==0xFF||code[-3]==0xFF||code[-2]==0xFF;
}
inline LONG CALLBACK Handler(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record=info->ExceptionRecord;
    switch(record->ExceptionCode) {
    case 0xC0000005: case 0xC0000006: case 0xC000001D: case 0xC0000096: case 0xC0000094:
    case 0xC000008C: case 0xC00000FD: case 0xC0000374: case 0xC0000409: break;
    default: return EXCEPTION_CONTINUE_SEARCH;
    }
    static volatile LONG reports=0;
    if(InterlockedIncrement(&reports)>12) return EXCEPTION_CONTINUE_SEARCH;
    char where[160],line[700];
    const auto address=reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    Describe(where,sizeof(where),address);
    const char* kind="";
    unsigned target=0;
    if(record->ExceptionCode==0xC0000005&&record->NumberParameters>=2) {
        kind=record->ExceptionInformation[0]==0?"read of":record->ExceptionInformation[0]==1?"write to":"execute at";
        target=unsigned(record->ExceptionInformation[1]);
    }
    std::snprintf(line,sizeof(line),"%llu EXCEPTION (first chance; harmless if the game carries on) code=%08lX at 0x%08X = %s; %s 0x%08X; last plugin stage=%s; frame=%lu; thread=%lu\n",
        static_cast<unsigned long long>(GetTickCount64()),record->ExceptionCode,unsigned(address),where,kind,target,stage,frames,GetCurrentThreadId());
    Raw(line);
    const CONTEXT* c=info->ContextRecord;
    std::snprintf(line,sizeof(line),"  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX eip=%08lX\n",
        c->Eax,c->Ebx,c->Ecx,c->Edx,c->Esi,c->Edi,c->Ebp,c->Esp,c->Eip);
    Raw(line);
    const auto* stack=reinterpret_cast<const uintptr_t*>(c->Esp);
    int shown=0;
    for(int i=0;i<192&&shown<28;++i) {
        if(!Readable(stack+i,sizeof(uintptr_t))) break;
        const uintptr_t value=stack[i];
        if(!LooksLikeReturnAddress(value)) continue;
        Describe(where,sizeof(where),value);
        std::snprintf(line,sizeof(line),"  stack[esp+0x%X] = 0x%08X = %s\n",unsigned(i*4),unsigned(value),where);
        Raw(line);++shown;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
inline DWORD WINAPI Watchdog(LPVOID) {
    unsigned long last=frames;
    int still=0,reports=0;
    char line[300];
    for(;;) {
        Sleep(1000);
        const unsigned long now=frames;
        if(now!=last) {
            if(still>=3) {
                std::snprintf(line,sizeof(line),"%llu WATCHDOG: frames resumed after about %d s\n",static_cast<unsigned long long>(GetTickCount64()),still);
                Raw(line);
            }
            last=now;still=0;continue;
        }
        ++still;
        if(reports<30&&(still==3||still%10==0)) {
            ++reports;
            std::snprintf(line,sizeof(line),"%llu WATCHDOG: no frame drawn for about %d s; last plugin stage=%s; frame=%lu\n",static_cast<unsigned long long>(GetTickCount64()),still,stage,now);
            Raw(line);
        }
    }
}
inline void ListModules() {
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snapshot==INVALID_HANDLE_VALUE) return;
    MODULEENTRY32 module{};module.dwSize=sizeof(module);
    char line[400];
    std::snprintf(line,sizeof(line),"%llu diag: loaded modules\n",static_cast<unsigned long long>(GetTickCount64()));
    Raw(line);
    for(BOOL ok=Module32First(snapshot,&module);ok;ok=Module32Next(snapshot,&module)) {
        std::snprintf(line,sizeof(line),"  %08X %s\n",unsigned(reinterpret_cast<uintptr_t>(module.modBaseAddr)),module.szExePath);
        Raw(line);
    }
    CloseHandle(snapshot);
}
inline void Install(const std::string& logPath) {
    std::snprintf(path,sizeof(path),"%s",logPath.c_str());
    AddVectoredExceptionHandler(1,&Handler);
}
inline void StartWatchdog() {
    static bool started=false;
    if(started) return;
    started=true;
    if(const HANDLE thread=CreateThread(nullptr,0,&Watchdog,nullptr,0,nullptr)) CloseHandle(thread);
}

} // namespace vicebounce::diag
#define DIAG_STAGE(name) (::vicebounce::diag::stage=(name))
#else
#define DIAG_STAGE(name) ((void)0)
#endif
