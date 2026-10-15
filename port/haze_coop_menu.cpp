#include "haze_coop.h"
#include "haze_menu.h"
#include <mutex>
#include <algorithm>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {
std::mutex action_mutex;
HazeCoopAction action;
bool hosting=false, lan=false;
std::string code, previous_code, feedback;
HazeMenuModal dialog;
unsigned revision=0;
std::string formatted(std::string value) {
    if(value.size()==12) value.insert(8,"-").insert(4,"-");
    return value;
}
void text() { dialog.message="Invite code: "+formatted(code)+"_\n"+(feedback.empty() ? "Enter the 12-character code from your host. Ctrl+V pastes a code." : feedback); }
void copy_code(const std::string& value) {
    std::wstring wide(value.begin(),value.end());
    const auto memory=GlobalAlloc(GMEM_MOVEABLE,(wide.size()+1)*sizeof(wchar_t));
    if(!memory) { feedback="Could not copy the invite code."; return; }
    auto data=static_cast<wchar_t*>(GlobalLock(memory));
    if(!data) { GlobalFree(memory); feedback="Could not copy the invite code."; return; }
    std::copy(wide.c_str(),wide.c_str()+wide.size()+1,data); GlobalUnlock(memory);
    bool copied=false;
    if(OpenClipboard(static_cast<HWND>(haze_menu_owner()))) {
        if(EmptyClipboard()) copied=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
        CloseClipboard();
    }
    if(!copied) GlobalFree(memory);
    feedback=copied ? "Invite code copied.":"Could not copy the invite code.";
}
void paste_code() {
    if(!OpenClipboard(static_cast<HWND>(haze_menu_owner()))) return;
    const auto memory=GetClipboardData(CF_UNICODETEXT);
    const auto data=memory ? static_cast<const wchar_t*>(GlobalLock(memory)):nullptr;
    std::string value;
    if(data) {
        const auto bound=std::min<size_t>(GlobalSize(memory)/sizeof(wchar_t),128);
        for(size_t i=0;i<bound && data[i];++i) {
            if(data[i]>127) { value.clear(); break; }
            value+=char(data[i]);
        }
        GlobalUnlock(memory);
    }
    CloseClipboard();
    value=haze::net::normalize_code(value);
    if(value.empty()) feedback="The clipboard does not contain a valid invite code.";
    else { code=std::move(value); feedback.clear(); }
}
std::vector<HazeMenuRow> rows() {
    const auto state=haze_coop_status();
    HazeMenuRow mode; mode.key="mode"; mode.label="Connection"; mode.value=lan ? "lan":"internet";
    mode.choices={{"internet","Internet invite code"},{"lan","Local network"}};
    if(lan) return {mode};
    HazeMenuRow invite; invite.key="invite"; invite.label=hosting ? (state.code.empty() ? "Create invite code":"Copy invite code"):"Enter invite code";
    invite.action=true; invite.value=formatted(hosting ? state.code:code);
    HazeMenuRow status; status.key="status"; status.label="Status";
    using haze::net::State;
    switch(state.state) {
    case State::Offline: status.value=hosting ? "Create an invite to host co-op.":"Enter a code, then connect to your host."; break;
    case State::Connecting: status.value="Connecting to co-op service..."; break;
    case State::Waiting: status.value=hosting ? "Invite ready. Share it, then continue to your campaign.":"Connecting to host..."; break;
    case State::Connected: status.value=std::to_string(state.peers)+" peer(s) connected"; break;
    case State::Failed: status.value=state.error; break;
    }
    if(!feedback.empty()) status.value=feedback;
    HazeMenuRow connect; connect.key="connect"; connect.label=hosting ? "Refresh invite code":"Connect to host"; connect.action=true;
    if(hosting) return {mode,invite,status};
    HazeMenuRow character; character.key="character"; character.label="Add code character";
    for(char c:std::string("ABCDEFGHJKLMNPQRSTUVWXYZ23456789")) character.choices.push_back({std::string(1,c),std::string(1,c)});
    HazeMenuRow erase; erase.key="erase"; erase.label="Delete last character"; erase.action=true;
    return {mode,invite,connect,status,character,erase};
}
void connect() {
    feedback.clear();
    if(hosting) haze_coop_host();
    else if(haze::net::normalize_code(code).empty()) feedback="Enter a valid 12-character invite code.";
    else haze_coop_join(code);
}
void append(uint32_t character) {
    if(character>='a' && character<='z') character-=32;
    if(character==8) { if(!code.empty()) code.pop_back(); }
    else if(character=='-' || character==' ' || character==13 || character==27) return;
    else if(code.size()<12 && std::string_view("ABCDEFGHJKLMNPQRSTUVWXYZ23456789").find(char(character))!=std::string_view::npos && character<128) code+=char(character);
    feedback.clear();
}
}
HazeCoopAction haze_coop_take_action() { std::lock_guard lock(action_mutex); auto value=action; action={}; return value; }
void haze_coop_menu_register() {
    for(bool host:{true,false}) {
        HazeMenuProvider provider;
        provider.title=host ? "HOST CO-OP":"JOIN CO-OP";
        provider.begin=[host] { hosting=host; lan=false; feedback.clear(); dialog={}; };
        provider.rows=rows;
        provider.change=[](const std::string& key,const std::string& value) {
            if(key=="mode") { haze_coop_leave(); lan=value=="lan"; feedback.clear(); }
            if(key=="character" && value.size()==1) append(uint8_t(value[0]));
        };
        provider.activate=[](const std::string& key) {
            if(key=="invite") {
                if(hosting) {
                    const auto state=haze_coop_status();
                    if(state.code.empty()) connect(); else copy_code(formatted(state.code));
                } else { previous_code=code; feedback.clear(); dialog={++revision,"Invite code",{}, {"Use code","Cancel"},true,1,true}; text(); }
            } else if(key=="connect") connect();
            else if(key=="erase" && !code.empty()) code.pop_back();
        };
        provider.apply=[](std::string& notice) {
            const auto state=haze_coop_status();
            if(!lan && (hosting ? state.code.empty() || state.state==haze::net::State::Failed : state.state!=haze::net::State::Connected)) {
                notice=hosting ? "Create an invite code before continuing.":"Connect to the host before continuing.";
                return HazeMenuApply::Failed;
            }
            { std::lock_guard lock(action_mutex); action={haze_menu_view().generation,hosting}; }
            notice="Opening campaign..."; return HazeMenuApply::Applied;
        };
        provider.defaults=[] { haze_coop_leave(); feedback.clear(); };
        provider.end=[](bool back) { if(back) haze_coop_leave(); dialog={}; };
        provider.modal=[] { return dialog; };
        provider.modal_choose=[](int choice) {
            if(choice==0 && haze::net::normalize_code(code).empty()) { feedback="Enter all 12 characters."; text(); return; }
            if(choice!=0) code=previous_code;
            dialog={}; feedback.clear();
        };
        provider.modal_key=[](int key) { if(key==13 && !haze::net::normalize_code(code).empty()) dialog={}; };
        provider.modal_text=[](uint32_t c) { if(dialog.text_entry) { if(c==22) paste_code(); else append(c); text(); } };
        provider.update=[](bool focused) { if(!focused && dialog.text_entry) { code=previous_code; dialog={}; } };
        provider.actions={"Continue","Disconnect","Back"};
        provider.defaults_notice="Co-op session disconnected.";
        provider.footer_hint=host ? "Share your invite code. Continue to choose a campaign checkpoint.":"Connect with an invite code, then continue to select the host's game.";
        haze_menu_register(host ? HazeMenuPage::CoopHost:HazeMenuPage::CoopJoin,std::move(provider));
    }
}
