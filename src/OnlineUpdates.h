#pragma once
#include "OnlineManifest.h"
#include <windows.h>
#include <winhttp.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <ctime>
#pragma comment(lib,"winhttp.lib")

namespace vicebounce {
// One fixed HTTPS metadata endpoint. No remote commands, URLs, DLLs or assets.
inline constexpr wchar_t OnlineHost[]=L"raw.githubusercontent.com";
inline constexpr wchar_t OnlinePath[]=L"/tek-95/ViceBounce/main/update.ini";
struct OnlineSnapshot {OnlineManifest manifest;uint64_t success=0;bool valid=false;std::string error;};
class OnlineUpdates {
    struct State {std::mutex lock;OnlineSnapshot snapshot;bool busy=false;ULONGLONG due=0;};
    struct Job {std::shared_ptr<State> state;std::filesystem::path cache;unsigned interval;HMODULE module=nullptr;};
    std::shared_ptr<State> state=std::make_shared<State>();
    std::filesystem::path cachePath;
    unsigned interval=86400;bool enabled=true;
    struct Internet {
        HINTERNET value=nullptr;
        explicit Internet(HINTERNET v):value(v){}
        ~Internet(){if(value)WinHttpCloseHandle(value);}
        operator HINTERNET() const{return value;}
    };
    static std::string ReadCache(const std::filesystem::path& path){
        std::ifstream in(path,std::ios::binary);if(!in)return {};
        std::string raw;char buf[512];
        while(in.read(buf,sizeof(buf))||in.gcount()){
            raw.append(buf,size_t(in.gcount()));if(raw.size()>4224)return {};
        }return raw;
    }
    static void SaveCache(const std::filesystem::path& path,const OnlineCache& cache){
        auto temporary=path;temporary+=L".tmp";
        {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)return;out<<cache.Encode();if(!out)return;}
        MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
    }
    static bool Fetch(std::string& body,std::string& error){
        Internet session(WinHttpOpen(L"ViceBounce/1.0.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        if(!session){error="HTTP unavailable";return false;}
        if(!WinHttpSetTimeouts(session,2000,2000,2000,2000)){error="timeout setup failed";return false;}
        Internet connection(WinHttpConnect(session,OnlineHost,INTERNET_DEFAULT_HTTPS_PORT,0));
        if(!connection){error="connection failed";return false;}
        Internet request(WinHttpOpenRequest(connection,L"GET",OnlinePath,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        if(!request){error="request failed";return false;}
        DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy))){error="redirect policy failed";return false;}
        const auto start=GetTickCount64();
        if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr)){
            error="offline or TLS/network failure";return false;
        }
        DWORD status=0,size=sizeof(status);
        if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200){
            error="HTTP "+std::to_string(status);return false;
        }
        char buffer[512];DWORD count=0;body.clear();
        for(;;){
            if(GetTickCount64()-start>8000){error="response timed out";return false;}
            if(!WinHttpReadData(request,buffer,sizeof(buffer),&count)){error="read failed";return false;}
            if(!count)return true;
            body.append(buffer,count);if(body.size()>4096){error="metadata too large";return false;}
        }
    }
    static DWORD WINAPI Worker(void* opaque){
        auto job=std::unique_ptr<Job>(static_cast<Job*>(opaque));const HMODULE module=job->module;
        try {
            const auto now=uint64_t(std::time(nullptr));OnlineCache cache;
            OnlineCache::Parse(ReadCache(job->cache),cache);
            OnlineManifest manifest;OnlineSnapshot result;
            if(OnlineManifest::Parse(cache.body,manifest))result={manifest,cache.success,true,{}};
            {std::lock_guard<std::mutex> guard(job->state->lock);job->state->snapshot=result;}
            auto delay=cache.Delay(now,job->interval);
            if(!delay){
                std::string body,error;const bool received=Fetch(body,error);
                if(AcceptOnlineResponse(cache,now,received,body,manifest)){
                    result={manifest,now,true,{}};
                }else result.error=error.empty()?"invalid metadata":error;
                SaveCache(job->cache,cache);delay=job->interval;
            }
            {std::lock_guard<std::mutex> guard(job->state->lock);job->state->snapshot=result;job->state->due=GetTickCount64()+delay*1000;job->state->busy=false;}
        }catch(...){
            std::lock_guard<std::mutex> guard(job->state->lock);job->state->snapshot.error="metadata check failed";
            job->state->due=GetTickCount64()+3600000;job->state->busy=false;
        }
        job.reset();
        // Keep this ASI loaded until its worker finishes; never wait under DllMain.
        if(module)FreeLibraryAndExitThread(module,0);
        return 0;
    }
public:
    void Configure(const std::wstring& directory,bool allow,unsigned hours){
        enabled=allow;interval=(hours<1?1:hours>168?168:hours)*3600;
        cachePath=std::filesystem::path(directory)/L"ViceBounceUpdate.cache";
    }
    void OnMenu(){
        if(!enabled||cachePath.empty())return;
        std::unique_lock<std::mutex> guard(state->lock,std::try_to_lock);
        if(!guard||state->busy||GetTickCount64()<state->due)return;
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&module))return;
        auto job=std::make_unique<Job>();job->state=state;job->cache=cachePath;job->interval=interval;job->module=module;
        state->busy=true;
        HANDLE thread=CreateThread(nullptr,0,&Worker,job.get(),0,nullptr);
        if(!thread){state->busy=false;state->due=GetTickCount64()+60000;FreeLibrary(module);return;}
        job.release();CloseHandle(thread);
    }
    OnlineSnapshot Snapshot() const {
        std::unique_lock<std::mutex> guard(state->lock,std::try_to_lock);
        return guard?state->snapshot:OnlineSnapshot{};
    }
};
}
