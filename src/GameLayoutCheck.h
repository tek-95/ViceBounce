#pragma once
// Verification of a GameLayout against a running (or loaded-from-disk) Vice City executable.
//
// `Memory` supplies the executable's contents and must provide:
//   const unsigned char* At(uintptr_t gameAddress) const   pointer to readable bytes at that address
//   uintptr_t Abs(uintptr_t gameAddress) const             absolute address that a pointer stored in
//                                                          the executable and referring to gameAddress
//                                                          holds at run time (identity for a fixed image base)
// The plugin uses the live process; tools/VerifyExe.cpp uses the .exe file. Both run exactly this code.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "GameLayout.h"

namespace vicebounce {

namespace detail {
template<class Memory> uint32_t ReadU32(const Memory& memory,uintptr_t address) {
    uint32_t value=0;std::memcpy(&value,memory.At(address),sizeof(value));return value;
}
// A `call rel32` at `address` whose destination is `target`.
template<class Memory> bool CallMatches(const Memory& memory,uintptr_t address,uintptr_t target) {
    const unsigned char* code=memory.At(address);
    int32_t rel=0;std::memcpy(&rel,code+1,sizeof(rel));
    return code[0]==0xE8&&uint32_t(address+5+uint32_t(rel))==uint32_t(target);
}
inline std::string Hex(uintptr_t value) {
    char text[16];std::snprintf(text,sizeof(text),"%08X",unsigned(value));return text;
}
}

// Returns true when every address in `layout` is confirmed. On failure `failure` names the first thing that
// did not match, so a log line is enough to tell which part of an unknown executable differs.
template<class Memory> bool VerifyLayout(const GameLayout& layout,const Memory& memory,std::string& failure) {
    const struct { const char* name; const VehicleHook* hook; } transports[]={
        {"CAutomobile",&layout.automobile},{"CBoat",&layout.boat},{"CHeli",&layout.heli},
        {"CPlane",&layout.plane},{"CBike",&layout.bike}};
    for(const auto& transport:transports) {
        const auto& hook=*transport.hook;
        if(detail::ReadU32(memory,hook.vtable+12*4)!=memory.Abs(hook.preRender)||
           detail::ReadU32(memory,hook.vtable+13*4)!=memory.Abs(hook.render)) {
            failure=std::string(transport.name)+" vtable at "+detail::Hex(hook.vtable)+" differs from the verified "+layout.name+" executable";
            return false;
        }
    }
    const struct { int slot; uintptr_t function; const char* what; } cutsceneSlots[]={
        {2,layout.cutsceneDestructor,"CCutsceneObject destructor"},
        {6,layout.cutsceneDeleteRw,"CCutsceneObject::DeleteRwObject"},
        {13,layout.cutsceneRender,"CCutsceneObject::Render"}};
    for(const auto& slot:cutsceneSlots) {
        if(detail::ReadU32(memory,layout.cutsceneVtable+slot.slot*4)!=memory.Abs(slot.function)) {
            failure=std::string(slot.what)+" vtable slot "+std::to_string(slot.slot)+" at "+detail::Hex(layout.cutsceneVtable)+" differs from the verified "+layout.name+" executable";
            return false;
        }
    }
    for(const auto& call:layout.calls) if(!detail::CallMatches(memory,call.address,call.target)) {
        failure=std::string(call.what)+" at "+detail::Hex(call.address)+" does not call "+detail::Hex(call.target);
        return false;
    }
    for(const auto& signature:layout.signatures) {
        if(std::memcmp(memory.At(signature.address),signature.bytes,signature.size)!=0) {
            failure=std::string("signature mismatch: ")+signature.what+" at "+detail::Hex(signature.address);
            return false;
        }
    }
    for(const auto& site:layout.lightCallsites) if(!detail::CallMatches(memory,site.address,site.target)) {
        failure="automobile light callsite at "+detail::Hex(site.address)+" does not call "+detail::Hex(site.target);
        return false;
    }
    return true;
}

} // namespace vicebounce
