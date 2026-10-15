#include <cassert>
#include <map>
#include "../port/haze_coop.h"
#include "../port/haze_menu.h"
#include "../port/net/invite_code.cpp"

static std::map<HazeMenuPage,HazeMenuProvider> providers;
static haze::net::Status state;
static unsigned hosts=0, joins=0, leaves=0;
static std::string joined_code;
void haze_menu_register(HazeMenuPage page,HazeMenuProvider provider) { providers[page]=std::move(provider); }
void* haze_menu_owner() { return nullptr; }
HazeMenuView haze_menu_view() { HazeMenuView view; view.generation=42; return view; }
haze::net::Status haze_coop_status() { return state; }
void haze_coop_host() { ++hosts; }
void haze_coop_join(const std::string& code) { ++joins; joined_code=code; }
void haze_coop_leave() { ++leaves; state={}; }
#include "../port/haze_coop_menu.cpp"

int main() {
    haze_coop_menu_register();
    auto& host=providers.at(HazeMenuPage::CoopHost);
    auto& join=providers.at(HazeMenuPage::CoopJoin);
    std::string notice;
    host.begin();
    assert(host.apply(notice)==HazeMenuApply::Failed);
    host.activate("invite"); assert(hosts==1);
    state.state=haze::net::State::Waiting; state.code="ABCDEFGHJK23";
    assert(host.apply(notice)==HazeMenuApply::Applied);
    auto action=haze_coop_take_action(); assert(action.generation==42 && action.host);
    assert(!haze_coop_take_action().generation);
    host.end(false); assert(leaves==0); // Continue preserves the room.
    host.end(true); assert(leaves==1); // Back closes an unfinished room.

    join.begin(); join.activate("connect"); assert(joins==0);
    join.activate("invite");
    for(char c:std::string("abcd-efgh-jk23")) join.modal_text(c);
    join.modal_choose(1); // Cancel restores the previous code.
    join.activate("connect"); assert(joins==0);
    join.activate("invite");
    for(char c:std::string("abcd-efgh-jk23")) join.modal_text(c);
    join.modal_choose(0); assert(!join.modal().text_entry);
    join.activate("connect"); assert(joins==1 && joined_code=="ABCDEFGHJK23");
    state.state=haze::net::State::Waiting;
    assert(join.apply(notice)==HazeMenuApply::Failed);
    state.state=haze::net::State::Connected;
    assert(join.apply(notice)==HazeMenuApply::Applied);
    action=haze_coop_take_action(); assert(action.generation==42 && !action.host);
    join.activate("invite"); join.modal_text(8); join.update(false);
    join.activate("connect"); assert(joined_code=="ABCDEFGHJK23");
    join.change("mode","lan"); assert(leaves==2);
    assert(join.apply(notice)==HazeMenuApply::Applied);
    join.defaults(); assert(leaves==3);
}
