// Included inside the plugin namespace, after Focused/SaveValue.
// The club branch has its own menu; this standalone menu has no VC:MP dependencies.
using MenuAction=panel::Action;
panel::Page modMenuPage=panel::Page::Home;
bool modMenuOpen=false;
volatile LONG modMenuToggleRequested=0,modMenuPendingKeys=0,modMenuToggleKey=0;
MenuAction modMenuSelected=MenuAction::Track;
ULONGLONG modMenuLastInput=0;
// A moving trapezoid like the frontend's option highlight (reVC Frontend.cpp
// menuOptionHighlight): on open, page change and every Up/Down move its corners get new
// random points inside fixed bounds and are reached in 50/255 steps every 20 ms
// (about 0.1 s). Corners in design units from the content origin:
// TLx,TLy, TRx,TRy, BLx,BLy, BRx,BRy.
struct ModMenuTrapezoid {
    float from[8]{},to[8]{},offset[8]{};ULONGLONG start=0;bool valid=false;
    int page=-1;MenuAction selected=MenuAction::None;
    bool Settled()const{return !valid||GetTickCount64()-start>=120;} // 6 steps of 20 ms reach 255
};
ModMenuTrapezoid modMenuFrame,modMenuHighlight;
unsigned modMenuSeed=0;
float ModMenuRandom(unsigned range){modMenuSeed=modMenuSeed*1103515245u+12345u;return float((modMenuSeed>>16)%range);}
// box: the shape without jitter; jitter(offset): fills new random offsets.
// slideIn: a new target slides from the current shape; otherwise the first one is shown at once.
// followSelection: new corners on every page change and Up/Down move; otherwise only on open.
template<class Jitter> void ModMenuMove(ModMenuTrapezoid& s,const float (&box)[8],Jitter jitter,bool slideIn,bool followSelection,float (&out)[8]){
    const auto now=GetTickCount64();
    const float blend=float((std::min)(255ull,50ull*((now-s.start)/20)))/255.f;
    float current[8];for(int i=0;i<8;++i)current[i]=s.from[i]+(s.to[i]-s.from[i])*blend;
    const bool moved=!s.valid||(followSelection&&(s.page!=int(modMenuPage)||s.selected!=modMenuSelected));
    if(moved){
        if(!s.valid){
            if(!modMenuSeed)modMenuSeed=unsigned(now);
            jitter(s.offset);for(int i=0;i<8;++i)current[i]=box[i]+s.offset[i];
        }
        if(s.valid||slideIn)jitter(s.offset);
        s.page=int(modMenuPage);s.selected=modMenuSelected;s.valid=true;
    }
    float target[8];for(int i=0;i<8;++i)target[i]=box[i]+s.offset[i];
    bool retarget=moved;for(int i=0;i<8&&!retarget;++i)retarget=std::fabs(target[i]-s.to[i])>.01f; // row or height moved
    if(retarget){std::copy(std::begin(current),std::end(current),s.from);std::copy(std::begin(target),std::end(target),s.to);s.start=now;
        for(int i=0;i<8;++i)current[i]=s.from[i];}
    std::copy(std::begin(current),std::end(current),out);
}
// Panel frame: new left corners only when the panel opens (2..11 units out, 0..6 up/down);
// a page change only follows the new height. The right corners are pinned to the
// screen's right edge by the renderer.
void ModMenuFrameJitter(float (&o)[8]){
    const float j[8]={-2-ModMenuRandom(10),-ModMenuRandom(7), 0,0, -2-ModMenuRandom(10),ModMenuRandom(7), 0,0};
    std::copy(std::begin(j),std::end(j),o);
}
// Selected row: the row's text spans y+3.4..y+9.2 of a 12-unit row (measured at
// 1920x1080), the neighbours' text ends at y-2.8 and starts at y+15.4. The top edge
// therefore moves within y-2..y+2 and the bottom within y+h-2..y+h+2, the sides 2..9
// units outside the text, so the highlight never crosses a letter of this row or the next.
void ModMenuHighlightJitter(float (&o)[8]){
    const float j[8]={-2-ModMenuRandom(8),2-ModMenuRandom(5), 2+ModMenuRandom(8),2-ModMenuRandom(5),
                      -2-ModMenuRandom(8),-2+ModMenuRandom(5), 2+ModMenuRandom(8),-2+ModMenuRandom(5)};
    std::copy(std::begin(j),std::end(j),o);
}
void CloseModMenu(){modMenuOpen=false;modMenuFrame.valid=false;modMenuHighlight.valid=false;}
bool ModMenuAvailable(){
    return onlineFontReady;
}
// The open panel takes only its own keys: arrows, Enter, Backspace and Esc (close),
// plus the F11 toggle. Every other key and the mouse reach the game, so the player
// keeps walking, driving and shooting with the panel open.
constexpr int modMenuKeys[]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_RETURN,VK_BACK};
bool ModMenuOwnsKey(WPARAM key){
    return key==VK_ESCAPE||std::find(std::begin(modMenuKeys),std::end(modMenuKeys),int(key))!=std::end(modMenuKeys);
}
// Consume menu input before GTA sees it, including while its frontend is open.
// Async key polling remains available to this panel. No game-code patch is needed.
WNDPROC modMenuPreviousWndProc=nullptr;
HWND modMenuWindow=nullptr;
bool modMenuEatEscape=false;
LRESULT CALLBACK ModMenuWndProc(HWND window,UINT message,WPARAM key,LPARAM data){
    const bool keyboard=message==WM_KEYDOWN||message==WM_KEYUP||message==WM_CHAR;
    const auto& binding=HotkeyFor(Action::MusicPicker);
    const bool menuKey=key&&std::find(binding.keys.begin(),binding.keys.end(),int(key))!=binding.keys.end();
    if((message==WM_KEYDOWN||message==WM_KEYUP)&&menuKey&&
       !(GetAsyncKeyState(VK_CONTROL)&0x8000)&&!(GetAsyncKeyState(VK_SHIFT)&0x8000)&&!(GetAsyncKeyState(VK_MENU)&0x8000)){
        if(message==WM_KEYDOWN&&!(data&(1u<<30))){InterlockedExchange(&modMenuToggleKey,LONG(key|((data>>16)&0x1FF)<<8));InterlockedExchange(&modMenuToggleRequested,1);}
        return 0;
    }
    // WM_CHAR carries a character, not a virtual key: only Enter, Backspace and Esc
    // produce one among the panel's keys.
    const bool ownChar=message==WM_CHAR&&(key=='\r'||key=='\b'||key==0x1B);
    if(modMenuOpen&&((message!=WM_CHAR&&keyboard&&ModMenuOwnsKey(key))||ownChar)){
        if(key==VK_ESCAPE&&message==WM_KEYDOWN){CloseModMenu();modMenuEatEscape=true;}
        if(message==WM_KEYDOWN&&!(data&(1u<<30)))
            for(int i=0;i<6;++i)if(int(key)==modMenuKeys[i])InterlockedOr(&modMenuPendingKeys,1<<i);
        return 0;
    }
    if(keyboard&&key==VK_ESCAPE&&modMenuEatEscape){if(message==WM_KEYUP)modMenuEatEscape=false;return 0;}
    return CallWindowProcW(modMenuPreviousWndProc,window,message,key,data);
}
void InstallModMenuWindow(){
    const auto window=GetForegroundWindow();
    if(window==modMenuWindow&&modMenuPreviousWndProc)return;
    SetLastError(0);
    const auto previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(ModMenuWndProc)));
    if(previous){modMenuPreviousWndProc=previous;modMenuWindow=window;}
    else Log("mod menu window input filter unavailable error="+std::to_string(GetLastError()));
}
void ClearModMenuKeys(){
    for(auto* keys:{&CPad::TempKeyState,&CPad::NewKeyState,&CPad::OldKeyState}){
        // Only the panel's own keys: one held while opening must not stay pressed
        // in the game after the panel swallows its key-up. Other keys stay live.
        keys->up=keys->down=keys->left=keys->right=0;
        keys->enter=keys->extenter=keys->back=keys->esc=0;
    }
}
bool MenuNews(const OnlineSnapshot& state){
    const auto now=uint64_t(std::time(nullptr));
    return onlineEnabled&&onlineAnnouncements&&state.valid&&!state.manifest.message.empty()&&
        now>=state.success&&now-state.success<=7*86400;
}

OnlineSnapshot ModMenuSnapshot(){
    static OnlineSnapshot cached;
    const auto latest=onlineUpdates.Snapshot();
    if(latest.valid)cached=latest; // A contended try-lock must not flicker notices or move keyboard focus.
    return cached;
}
panel::Data ModMenuData(){
    panel::Data data;
    const auto source=configuredMusicSource.empty()?musicFile:configuredMusicSource;
    if(!source.empty()){
        const auto name=std::filesystem::path(source).stem().wstring();
        // The game's font atlas cannot render arbitrary Unicode or font tokens.
        data.track=panel::TrackText(name);
        if(data.track.empty())data.track="No track selected";
    }
    data.playback=importer.Running()?"Preparing":!musicEnabled||musicFile.empty()?"Stopped":music.Error()?"Unavailable":musicPaused?"Paused":"Playing";
    data.cars=cfg.enabled;data.other=cfg.otherVehicles;data.people=cfg.allPeople;
    data.plants=cfg.vegetation;data.buildings=cfg.buildings;data.street=cfg.streetObjects;
    data.strength=cfg.amplitude;data.bpm=cfg.bpm;data.uk=cfg.style==1;data.beatMap=useBeatMap&&beatMap.Valid();
    data.paused=musicPaused;
    const auto state=ModMenuSnapshot();
    if(onlineEnabled&&onlineVersions&&state.valid&&state.manifest.NewerThanCurrent())data.latest=state.manifest.latest;
    if(MenuNews(state)){data.news=state.manifest.message;data.newsLink=state.manifest.url;if(!state.manifest.label.empty())data.newsLabel=state.manifest.label;}
    return data;
}
void ModMenuBack(){
    if(modMenuPage==panel::Page::Home)CloseModMenu();
    else {modMenuPage=panel::Page::Home;modMenuSelected=MenuAction::More;}
}
void ModMenuChange(int direction){
    switch(modMenuSelected){
    case MenuAction::Track:CloseModMenu();importer.Start(musicDirectory,true);break;
    case MenuAction::Vehicles:{
        const bool enable=panel::EnableGroup(cfg.enabled,cfg.otherVehicles,cfg.otherVehicles);
        cfg.enabled=cfg.otherVehicles=enable;SaveValue("Enabled",enable);SaveValue("OtherVehicles",enable);break;
    }
    case MenuAction::World:{
        const bool enable=panel::EnableGroup(cfg.vegetation,cfg.buildings,cfg.streetObjects);
        cfg.vegetation=cfg.buildings=cfg.streetObjects=enable;
        SaveValue("Vegetation",enable);SaveValue("Buildings",enable);SaveValue("StreetObjects",enable);break;
    }
    case MenuAction::People:cfg.allPeople=!cfg.allPeople;SaveValue("AllPeople",cfg.allPeople);break;
    case MenuAction::Cars:cfg.enabled=!cfg.enabled;SaveValue("Enabled",cfg.enabled);break;
    case MenuAction::Other:cfg.otherVehicles=!cfg.otherVehicles;SaveValue("OtherVehicles",cfg.otherVehicles);break;
    case MenuAction::Plants:cfg.vegetation=!cfg.vegetation;SaveValue("Vegetation",cfg.vegetation);break;
    case MenuAction::Buildings:cfg.buildings=!cfg.buildings;SaveValue("Buildings",cfg.buildings);break;
    case MenuAction::Street:cfg.streetObjects=!cfg.streetObjects;SaveValue("StreetObjects",cfg.streetObjects);break;
    case MenuAction::Strength:cfg.amplitude=std::clamp(cfg.amplitude+direction*.025,0.,.75);SaveValue("Amplitude",cfg.amplitude);break;
    case MenuAction::Bpm:cfg.bpm=std::clamp(cfg.bpm+direction*5.,20.,300.);useBeatMap=false;SaveValue("MusicUseBeatMap",0);SaveValue("BPM",cfg.bpm);break;
    case MenuAction::Style:cfg.style=cfg.style==1?0:1;SaveValue("BounceStyle",cfg.style);break;
    case MenuAction::PauseMusic:musicPaused=!musicPaused;music.SetPaused(musicPaused);SaveValue("MusicPaused",musicPaused);break;
    case MenuAction::More:modMenuPage=panel::Page::Settings;modMenuSelected=MenuAction::Cars;break;
    case MenuAction::Read:modMenuPage=panel::Page::News;modMenuSelected=MenuAction::Back;break;
    case MenuAction::Release:
        CloseModMenu();ShellExecuteW(nullptr,L"open",ReleasesUrl,nullptr,nullptr,SW_SHOWNORMAL);break;
    case MenuAction::NewsLink:{
        const auto state=ModMenuSnapshot();
        if(MenuNews(state)&&!state.manifest.url.empty()&&AnnouncementUrl(state.manifest.url)){
            const std::wstring url(state.manifest.url.begin(),state.manifest.url.end());
            CloseModMenu();ShellExecuteW(nullptr,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        }
        break;
    }
    case MenuAction::Reload:LoadConfig(false);break;
    case MenuAction::Back:ModMenuBack();break;
    default:break;
    }
}
void ModMenuInput(){
    static bool held[6]{};
    bool edge[6]{};const auto pending=InterlockedExchange(&modMenuPendingKeys,0);
    for(int i=0;i<6;++i){const auto state=GetAsyncKeyState(modMenuKeys[i]);const bool down=(state&0x8000)!=0;edge[i]=modMenuPreviousWndProc?(pending&(1<<i))!=0:(down&&!held[i])||(!down&&(state&1));held[i]=down;}
    if(!modMenuOpen)return;
    if(!Focused()||!ModMenuAvailable()||GetTickCount64()-modMenuLastInput>30000){CloseModMenu();return;}
    for(int i=0;i<6;++i)if(edge[i]){
        const auto frame=panel::Build(modMenuPage,ModMenuData());
        modMenuSelected=panel::Normalize(modMenuSelected,frame);
        modMenuLastInput=GetTickCount64();
        if(i<2)modMenuSelected=panel::Move(modMenuSelected,i==0?-1:1,frame);
        else if(i==5)ModMenuBack();
        else if(i==4||panel::Adjustable(modMenuSelected))ModMenuChange(i==2?-1:1);
    }
    if(modMenuOpen)ClearModMenuKeys();
}
void DrawModMenu(){
    if(!modMenuOpen||!Focused()||!ModMenuAvailable()){if(modMenuOpen)CloseModMenu();return;}
    if(onlineEnabled)onlineUpdates.OnMenu();
    const auto data=ModMenuData();const auto frame=panel::Build(modMenuPage,data);
    modMenuSelected=panel::Normalize(modMenuSelected,frame);
    RECT rect{};if(!GetClientRect(GetForegroundWindow(),&rect)||rect.right<320||rect.bottom<200)return;
    const auto t=club2d::Make(float(rect.right),float(rect.bottom),club2d::AnchorRight);
    const float left=t.X(408.f),top=t.Y(140.f),width=t.W(218.f);
    const auto font=MenuFontFor(gameExe);
    auto& details=*reinterpret_cast<CFontDetails*>(plugin::GetGlobalAddress(font.details));
    std::array<unsigned char,sizeof(CFontDetails)> saved;std::memcpy(saved.data(),&details,saved.size());
    using Flush=void(__cdecl*)();using Print=void(__cdecl*)(float,float,const wchar_t*);
    using DrawRect=void(__cdecl*)(const CRect&,const CRGBA&);
    using SetStyle=void(__cdecl*)(short);
    const auto flush=reinterpret_cast<Flush>(plugin::GetGlobalAddress(font.draw));
    const auto print=reinterpret_cast<Print>(plugin::GetGlobalAddress(font.print));
    const auto drawRect=reinterpret_cast<DrawRect>(plugin::GetGlobalAddress(font.rect));
    const auto setStyle=reinterpret_cast<SetStyle>(plugin::GetGlobalAddress(font.style));
    flush();
    const float x0=left-t.W(panel::PanelPadding),y0=top-t.H(panel::PanelPadding);
    const float x1=left+width+t.W(panel::PanelPadding),y1=top+t.H(frame.height+panel::PanelPadding);
    const CRGBA surface(18,12,28,235);
    using Quad=void(__cdecl*)(float,float,float,float,float,float,float,float,const CRGBA&,const CRGBA&,const CRGBA&,const CRGBA&);
    const auto quad=menuQuadReady?reinterpret_cast<Quad>(plugin::GetGlobalAddress(font.quad)):nullptr;
    if(quad){
        // Moving like the frontend's menu shapes; the right corners stay on the screen edge.
        const float p=panel::PanelPadding,w=panel::PanelWidth,h=frame.height;
        const float box[8]={-p,-p, w+p,-p, -p,h+p, w+p,h+p};
        float c[8];ModMenuMove(modMenuFrame,box,ModMenuFrameJitter,true,false,c);
        const float edge=float(rect.right);
        quad(left+t.W(c[0]),top+t.H(c[1]),edge,top+t.H(-p),left+t.W(c[4]),top+t.H(c[5]),edge,top+t.H(h+p),
            surface,surface,surface,surface);
    }else drawRect(CRect(x0,y0,x1,y1),surface);
    // Announcement actions (link, Read more) are cyan, like the panel's other action values.
    auto announcement=[](MenuAction action){return action==MenuAction::NewsLink||action==MenuAction::Read;};
    // Submit rectangles before font batching; otherwise a selected row can cover
    // the previously queued labels when the engine flushes its font buffer.
    for(const auto& row:frame.rows){
        const float y=top+t.H(row.y);
        if(row.kind==panel::Kind::Rule)drawRect(CRect(left,y+t.H(3),left+width,y+t.H(3.5f)),CRGBA(105,74,109,230));
        // Only the selected row gets a background; a permanent tint on More settings
        // read as a second selection.
        if(row.action!=MenuAction::None&&row.action==modMenuSelected){
            const CRGBA fill(110,232,245,255); // every selection is cyan
            if(quad){
                // Slides from the previous row to this one with new random corners.
                const float x=row.x,r=row.x+row.width,b=row.y+row.height;
                const float box[8]={x,row.y, r,row.y, x,b, r,b};
                float c[8];ModMenuMove(modMenuHighlight,box,ModMenuHighlightJitter,false,true,c);
                quad(left+t.W(c[0]),top+t.H(c[1]),left+t.W(c[2]),top+t.H(c[3]),left+t.W(c[4]),top+t.H(c[5]),left+t.W(c[6]),top+t.H(c[7]),fill,fill,fill,fill);
            }else drawRect(CRect(left+t.W(row.x-2),y-t.H(1),left+t.W(row.x+row.width+2),y+t.H(row.height-1)),fill);
        }
    }
    details.m_fSlant=0;details.m_bJustify=false;details.m_bCentre=false;details.m_bRightJustify=false;
    details.m_bBackground=false;details.m_bBackGroundOnlyText=false;details.m_bPropOn=true;
    details.m_bForcedColor=false;details.m_fAlpha=255;details.m_FontStyle=1;details.m_bFontHalfTexture=false;
    details.m_fWrapX=left+width;details.m_fRightJustifyWrap=left;
    details.m_nShadowPos=0;details.m_DropColor=CRGBA(0,0,0,0);
    auto text=[&](float x,float y,const std::string& string,float size,CRGBA color,bool right=false,bool heading=false){
        setStyle(heading?2:1);
        details.m_Color=color;details.m_vecScale.x=t.W(size);details.m_vecScale.y=t.H(size*1.9f);
        details.m_bRightJustify=right;
        const std::wstring wide(string.begin(),string.end());print(x,y,wide.c_str());details.m_bRightJustify=false;
    };
    for(const auto& row:frame.rows){
        if(row.kind==panel::Kind::Rule)continue;
        const float y=top+t.H(row.y+(row.kind==panel::Kind::Row?1.5f:0.f)),x=left+t.W(row.x);
        // Dark selected text only once the sliding highlight is under it; while it is on
        // its way the row keeps its normal colour, so the label never disappears.
        const bool selected=row.action!=MenuAction::None&&row.action==modMenuSelected&&(!quad||modMenuHighlight.Settled());
        const CRGBA normal(241,232,245,255),pink(255,135,215,255),cyan(110,232,245,255),muted(180,163,187,255),ink(26,10,31,255);
        // The HUD's money green (reVC Hud.cpp MONEY_COLOR 0,207,133) marks the tempo and the
        // update notice, lifted a step: thin small type reads darker than the bold money digits.
        const CRGBA green(60,236,164,255);
        CRGBA color=selected?ink:row.kind==panel::Kind::Title||row.kind==panel::Kind::Heading?pink:row.kind==panel::Kind::Status?green:row.kind==panel::Kind::Hint||row.kind==panel::Kind::Muted?muted:normal;
        float size=panel::FontSize(row.kind);
        if(row.action==MenuAction::Release&&!selected)color=green;
        if(announcement(row.action)&&!selected)color=cyan;
        // Title shadow as in the frontend's page header (reVC Frontend.cpp): the same
        // text drawn first, 7 units left and down at header scale 1.0; 3 units at the
        // title's .46 scale. The frontend's (30,30,30) sits on black; on the plum panel
        // it vanished, so the shadow is the same step lighter than this background.
        if(row.kind==panel::Kind::Title)text(x-t.W(3),y+t.H(3),row.label,size,CRGBA(66,52,72,255),false,true);
        // A two-line label (long track name) prints one 12-unit line per '\n'.
        size_t start=0;float lineY=y;
        for(size_t end;(end=row.label.find('\n',start))!=std::string::npos;start=end+1,lineY+=t.H(12))
            text(x,lineY,row.label.substr(start,end-start),size,color,false,row.kind==panel::Kind::Title);
        text(x,lineY,row.label.substr(start),size,color,false,row.kind==panel::Kind::Title);
        if(!row.value.empty())text(left+width,y+t.H(row.kind==panel::Kind::Title?5.f:0.f),row.value,row.kind==panel::Kind::Title?.18f:panel::FontSize(row.kind),selected?ink:row.kind==panel::Kind::Title?muted:cyan,true);
    }
    flush();std::memcpy(&details,saved.data(),saved.size());
}
