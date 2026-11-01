#pragma once
// Aspect-correct 2D layout for ViceBounce's own on-screen drawing (F11 menu,
// club hint lines, dance labels). Header-only and free of plugin-sdk so the
// host test compiles it as is.
//
// Design space is the game's PC HUD space: 640 x 448 units (reVC
// core/common.h DEFAULT_SCREEN_WIDTH/HEIGHT for VC). Y keeps the game's own
// scale, H/448. X uses reVC's SCREEN_SCALE_X with ASPECT_RATIO_SCALE,
// a*W/640*(4/3)/(W/H) = a*H/480, so text and boxes keep their 4:3 shape on
// 16:10, 16:9 and 21:9 instead of stretching. On 4:3 (and narrower, e.g.
// 5:4, where H/480 would overflow the screen) X stays W/640: the 4:3 layout
// is bit-for-bit the stretched one we drew before.
//
// The 640-unit design canvas is then anchored: Left keeps x=0 at the left
// edge, Right keeps x=640 at the right edge (the F11 menu, which sits under
// the HUD's top-right block), Centre centres it (the club hint lines).
//
// Pixel sizes come only from RsGlobal.maximumWidth/Height, never from
// CDraw's aspect (WidescreenFix ForceAspectRatio may change that). Both the
// CSprite2d::DrawRect background and the CFont text are placed through the
// same Transform, so they stay aligned. WidescreenFix (new mode) offsets
// sprite and rect vertices only while its HUD flags are set inside
// CHud::Draw/DrawAfterFade; it clears them at every return of both, and our
// draws run outside them (drawHudEvent.after, drawingEvent.after). Legacy
// mode only rewrites the game's own 1/640 call-site constants, which this
// code does not use.
#include <algorithm>

namespace club2d {

constexpr float DesignWidth=640.f,DesignHeight=448.f;
// 4:3 reference height for the X scale (640 units at 4:3 are H*4/3 pixels).
constexpr float AspectHeight=480.f;

enum Anchor{AnchorLeft,AnchorCentre,AnchorRight};

struct Transform {
    float width=DesignWidth,height=DesignHeight; // real pixels
    float sx=1.f,sy=1.f;  // pixels per design unit
    float originX=0.f;    // pixel x of design x=0
    float X(float x)const{return originX+x*sx;}  // design x -> pixel x
    float Y(float y)const{return y*sy;}           // design y -> pixel y
    float W(float w)const{return w*sx;}           // design width -> pixels (also CFont X scale)
    float H(float h)const{return h*sy;}           // design height -> pixels (also CFont Y scale)
};

inline Transform Make(float width,float height,Anchor anchor) {
    Transform t;
    t.width=width>=1.f?width:DesignWidth;
    t.height=height>=1.f?height:AspectHeight; // unknown size: plain 640x480
    t.sy=t.height/DesignHeight;
    t.sx=(std::min)(t.width/DesignWidth,t.height/AspectHeight);
    const float span=DesignWidth*t.sx;
    t.originX=anchor==AnchorLeft?0.f:anchor==AnchorCentre?(t.width-span)*.5f:t.width-span;
    return t;
}

} // namespace club2d
