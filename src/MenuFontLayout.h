#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "SceneryLayout.h"

namespace vicebounce {
// quad: CSprite2d::DrawAnyRect(x1,y1,x2,y2,x3,y3,x4,y4,const CRGBA&x4), the call the
// frontend's skewed menu frame uses. Corners: 1 top-left, 2 top-right, 3 bottom-left,
// 4 bottom-right (reVC SetVertices fans 3,4,2,1).
struct MenuFontLayout {uintptr_t scale,print,draw,flush,details,rect,style,quad;};
inline MenuFontLayout MenuFontFor(GameExe exe){
    if(exe==GameExe::Retail10)return {0x550230,0x551040,0x550250,0x551A30,0x97F820,0x577B00,0x54FFE0,0x5779B0};
    // 1.1 quad: DrawRect's +0x20 shift; no local 1.1 sample, so it is only used after
    // VerifyMenuQuad matches at runtime (otherwise the panel stays rectangular).
    if(exe==GameExe::Retail11)return {0x550250,0x551060,0x550270,0x551A50,0x97F828,0x577B20,0x550000,0x5779D0};
    return {0x550120,0x550F30,0x550140,0x551920,0x97E828,0x5779F0,0x54FED0,0x5778A0};
}
// DrawAnyRect's first 0x40 bytes hold no absolute address (the SetVertices call is
// relative and identical in 1.0 and Steam), so they are byte-identical across builds.
template<class Memory> bool VerifyMenuQuad(GameExe exe,const Memory& memory){
    static const unsigned char prefix[]={
        0x53,0x56,0x8B,0x5C,0x24,0x38,0x57,0x8B,0x74,0x24,0x34,0x55,0x8B,0x6C,0x24,0x3C,
        0x8B,0x7C,0x24,0x34,0x53,0x55,0x56,0x57,0xFF,0x74,0x24,0x40,0xFF,0x74,0x24,0x40,
        0xFF,0x74,0x24,0x40,0xFF,0x74,0x24,0x40,0xFF,0x74,0x24,0x40,0xFF,0x74,0x24,0x40,
        0xFF,0x74,0x24,0x40,0xFF,0x74,0x24,0x40,0xE8,0xD3,0x07,0x00,0x00,0x83,0xC4,0x30};
    return std::memcmp(memory.At(MenuFontFor(exe).quad),prefix,sizeof(prefix))==0;
}
// Checked on the three local executable samples. Do not call raw SDK CFont
// methods: that older part of the SDK hard-codes the retail 1.0 addresses.
template<class Memory> bool VerifyMenuFont(GameExe exe,const Memory& memory){
    const auto f=MenuFontFor(exe);const auto* scale=memory.At(f.scale);const auto* print=memory.At(f.print);const auto* draw=memory.At(f.draw);
    const unsigned char load[]={0xD9,0x44,0x24,0x04,0xD9,0x1D};
    const unsigned char next[]={0xD9,0x44,0x24,0x08,0xD9,0x1D};
    const unsigned char prefix[]={0x53,0x56,0x57,0x55,0x83,0xEC,0x38,0x8B,0x74,0x24,0x54,0xC6,0x05};
    uint32_t x=0,y=0,flag=0;int32_t target=0;
    std::memcpy(&x,scale+6,4);std::memcpy(&y,scale+16,4);std::memcpy(&flag,print+13,4);std::memcpy(&target,draw+1,4);
    const unsigned char rectPrefix[]={0x8B,0x44,0x24,0x04,0x53,0x8B,0x5C,0x24,0x0C,0x53,0x53,0x53,0x53,0x50};
    const unsigned char stylePrefix[]={0x8B,0x4C,0x24,0x04,0x0F,0xBF,0xC1,0x83,0xE8,0x02,0x85,0xC0,0x77,0x19};
    return std::memcmp(memory.At(f.style),stylePrefix,sizeof(stylePrefix))==0&&std::memcmp(memory.At(f.rect),rectPrefix,sizeof(rectPrefix))==0&&std::memcmp(scale,load,6)==0&&std::memcmp(scale+10,next,6)==0&&scale[20]==0xC3&&
        x==memory.Abs(f.details+4)&&y==memory.Abs(f.details+8)&&
        std::memcmp(print,prefix,sizeof(prefix))==0&&flag==memory.Abs(f.details+31)&&print[17]==0&&
        draw[0]==0xE8&&draw[5]==0xC3&&uint32_t(f.draw+5+target)==f.flush;
}
}
