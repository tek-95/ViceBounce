// VerifyExe: checks a gta-vc.exe file against every executable layout ViceBounce knows.
//
// It runs the very same verification code the plugin runs at game startup (src/GameLayoutCheck.h),
// but against the file on disk, so you can tell offline whether ViceBounce will hook your executable.
//
//   clang++ -std=c++17 -O1 tools/VerifyExe.cpp -o VerifyExe   (or g++ / MSVC cl /std:c++17 /EHsc)
//   VerifyExe "C:\Games\Vice City\gta-vc.exe"
//
// Exit status 0: at least one layout matches. 1: none match. 2: not a usable 32-bit PE file.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "../src/GameLayoutCheck.h"

namespace {
struct ImageMemory {
    uint32_t base=0;
    std::vector<unsigned char> image;
    explicit ImageMemory(const std::vector<unsigned char>& file) {
        auto u16=[&](size_t at){ if(at+2>file.size()) throw std::runtime_error("truncated file"); uint16_t v;std::memcpy(&v,&file[at],2);return v; };
        auto u32=[&](size_t at){ if(at+4>file.size()) throw std::runtime_error("truncated file"); uint32_t v;std::memcpy(&v,&file[at],4);return v; };
        if(file.size()<0x40||file[0]!='M'||file[1]!='Z') throw std::runtime_error("not an MZ/PE file");
        const size_t pe=u32(0x3C);
        if(u32(pe)!=0x00004550) throw std::runtime_error("missing PE signature");
        if(u16(pe+4)!=0x014C) throw std::runtime_error("not a 32-bit x86 executable");
        const unsigned sections=u16(pe+6);
        const size_t optional=pe+24,optionalSize=u16(pe+20);
        if(u16(optional)!=0x10B) throw std::runtime_error("not a PE32 image");
        base=u32(optional+28);
        const uint32_t imageSize=u32(optional+56),headerSize=u32(optional+60);
        if(imageSize==0||imageSize>0x10000000) throw std::runtime_error("implausible image size");
        image.assign(imageSize,0);
        std::memcpy(image.data(),file.data(),std::min<size_t>(headerSize,file.size()));
        for(unsigned i=0;i<sections;++i) {
            const size_t entry=optional+optionalSize+40*i;
            const uint32_t virtualSize=u32(entry+8),virtualAddress=u32(entry+12),rawSize=u32(entry+16),rawPointer=u32(entry+20);
            const size_t copy=std::min<size_t>(rawSize,virtualSize?virtualSize:rawSize);
            if(rawPointer>file.size()||copy>file.size()-rawPointer||size_t(virtualAddress)+copy>image.size()) continue;
            std::memcpy(&image[virtualAddress],&file[rawPointer],copy);
        }
    }
    const unsigned char* At(uintptr_t address) const {
        const size_t offset=size_t(address)-base;
        if(address<base||offset+16>image.size()) throw std::runtime_error("address outside the executable image");
        return &image[offset];
    }
    uintptr_t Abs(uintptr_t address) const { return address; } // Vice City is a fixed-base image
    uint32_t Word(uintptr_t address) const { uint32_t v;std::memcpy(&v,At(address),4);return v; }
};
}

int main(int argc,char** argv) {
    if(argc!=2) { std::fprintf(stderr,"usage: %s <path to gta-vc.exe>\n",argv[0]);return 2; }
    std::ifstream input(argv[1],std::ios::binary);
    if(!input) { std::fprintf(stderr,"cannot open %s\n",argv[1]);return 2; }
    const std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    try {
        const ImageMemory memory(file);
        std::printf("%s: %zu bytes, image base %08X\n",argv[1],file.size(),memory.base);
        // The same markers Plugin-SDK uses to tell the Vice City executables apart.
        std::printf("Plugin-SDK version markers: 1.0 EN %s, 1.1 EN %s, Steam %s\n",
            memory.Word(0x667BF0)==0x53E58955?"yes":"no",
            memory.Word(0x667C40)==0x53E58955?"yes":"no",
            memory.Word(0x666BA0)==0x53E58955?"yes":"no");
        const vicebounce::GameLayout* layouts[]={&vicebounce::layout10EN,&vicebounce::layoutSteam};
        int matches=0;
        for(const auto* layout:layouts) {
            std::string failure;
            const bool ok=vicebounce::VerifyLayout(*layout,memory,failure);
            std::printf("  %-8s %s%s%s\n",layout->name,ok?"MATCH  (all checks pass; ViceBounce will hook this executable)":"no match",ok?"":": ",ok?"":failure.c_str());
            matches+=ok;
            if(ok) std::printf("           %zu transport hooks verified, %zu extra calls, %zu signatures, %zu light callsites\n",
                size_t(5),layout->calls.size(),layout->signatures.size(),layout->lightCallsites.size());
        }
        return matches?0:1;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"%s: %s\n",argv[1],error.what());return 2;
    }
}
