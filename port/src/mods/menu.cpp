#include "menu.hpp"

#include <algorithm>
#include <map>

#include "gamepad.hpp"
#include "gameutil.hpp"
#include "platform/input.hpp"
#include "platform/window.hpp"
#include "runtime.hpp"
#include "session.hpp"
#include "ui_framework.hpp"

namespace mods::menu {
namespace {
bool                     open = false;
bool                     pointer = false;
int                      selected = 0, first = 0, detail = 0;
float                    px = 320, py = 240;
unsigned                 buttons = 0;
std::map<int, UI::Label> texts;
UI::ListView             list({
    .bounds = {42, 150, 226, 189},
    .row_height = 24,
    .row_stride = 27,
    .visible_rows = 7,
    .wrap_navigation = false,
    .draw_cursor = false
});
UI::Cursor               cursor;
std::string              trust_id, trust_fingerprint, trust_permissions;
bool                     trust_value = false;
std::string              action_owner, action_error;
int                      action_selected = 0, action_detail = 0;
UI::ListView             action_list({
    .bounds = {42, 150, 226, 189},
    .row_height = 24,
    .row_stride = 27,
    .visible_rows = 7,
    .wrap_navigation = false,
    .draw_cursor = false
});

std::string quest_owner;
int quest_selected=0,quest_detail=0,quest_filter=0;
UI::ListView quest_list({
    .bounds={42,150,226,189}, .row_height=24, .row_stride=27,
    .visible_rows=7, .wrap_navigation=false, .draw_cursor=false
});

// Manifest strings are literal text, never game message control codes.
std::string Literal(std::string value) {
    for (std::size_t p = 0; (p = value.find('{', p)) != std::string::npos; p += 2) {
        value.insert(p, "{");
    }
    return value;
}

void Text(int slot, std::string text, int x, int y, int width = 560, bool yellow = false) {
    auto &label = texts[slot];
    label.SetColor(yellow ? UI::ColorGold : UI::ColorWhite);
    label.SetText(text);
    while (label.GetWidth() > width && !text.empty()) {
        text.pop_back();
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xc0) == 0x80) {
            text.pop_back();
        }
        if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xc0) {
            text.pop_back();
        }
        label.SetText(text + "...");
    }
    label.Draw(x, y);
}

std::vector<std::string> Lines(const std::string &value, int width) {
    std::vector<std::string> result;
    UI::Label                measure;
    std::string              line;
    for (std::size_t i = 0; i < value.size();) {
        const auto c = static_cast<unsigned char>(value[i]);
        const auto length = c < 0x80 ? 1u : c < 0xe0 ? 2u
                                        : c < 0xf0   ? 3u
                                                     : 4u;
        const auto ch = value.substr(i, length);
        i += ch.size();
        if (ch == "\n") {
            result.push_back(line);
            line.clear();
            continue;
        }
        measure.SetText(Literal(line + ch));
        if (measure.GetWidth() > width && !line.empty()) {
            const auto space = line.rfind(' ');
            if (space != std::string::npos) {
                result.push_back(line.substr(0, space));
                line.erase(0, space + 1);
            } else {
                result.push_back(line);
                line.clear();
            }
        }
        line += ch;
    }
    if (!line.empty()) {
        result.push_back(line);
    }
    return result;
}

void Clamp() {
    const auto size = static_cast<int>(Shared().Preview().Packages().size());
    selected = std::clamp(selected, 0, std::max(0, size - 1));
    list.SetSelectedIndex(selected, size);
    selected = list.SelectedIndex();
    first = list.FirstRow();
}

void Toggle() {
    const auto &packages = Shared().Preview().Packages();
    if (selected < static_cast<int>(packages.size())) {
        const auto id = packages[selected].id;
        Shared().SetEnabled(id, !packages[selected].enabled);
    }
    detail = 0;
}

void ReviewTrust() {
    const auto &packages = Shared().Preview().Packages();
    if (selected >= static_cast<int>(packages.size())) {
        return;
    }
    const auto &p = packages[selected];
    if (p.native_library.empty()) {
        return;
    }
    trust_id = p.id;
    trust_fingerprint = p.native_fingerprint;
    trust_value = !p.native_trusted;
    trust_permissions.clear();
    for (const auto &permission : p.permissions) {
        trust_permissions += permission + " ";
    }
}

bool Inside(int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }

std::string SelectedActionOwner() {
    const auto &packages = Shared().Preview().Packages();
    if (selected < 0 || selected >= static_cast<int>(packages.size())) {
        return {};
    }
    const auto &p = packages[selected];
    for (const auto &active : Shared().Active().Packages()) {
        if (active.root == p.root && active.id == p.id && active.Active() && !Scripts().Actions(p.id).empty()) {
            return p.id;
        }
    }
    return {};
}

std::string SelectedQuestOwner() {
    const auto &packages=Shared().Preview().Packages();
    if(selected<0||selected>=static_cast<int>(packages.size()))return {};
    const auto &p=packages[selected];
    for(const auto &active:Shared().Active().Packages())
        if(active.root==p.root&&active.id==p.id&&active.Active()&&!active.quests.empty())return p.id;
    return {};
}

std::vector<QuestStatus> JournalQuests() {
    auto entries=Scripts().Quests();
    std::erase_if(entries,[](const auto &q){return q.owner!=quest_owner||(quest_filter==1&&q.progress>=q.goal)||(quest_filter==2&&q.progress<q.goal);});
    return entries;
}

void OpenQuests() {
    quest_owner=SelectedQuestOwner();quest_selected=quest_detail=quest_filter=0;quest_list.Reset();
}

void RunQuests(std::uint16_t pressed) {
    if(pressed)pointer=false;
    const auto mouse=InputTakeMenuMouse();int w=640,h=480;WindowSize(w,h);
    const float scale=1.0f/std::max(0.1f,std::min(w/640.0f,h/480.0f));
    px=std::clamp(px+mouse.dx*scale,0.0f,639.0f);py=std::clamp(py+mouse.dy*scale,0.0f,479.0f);
    const auto clicked=mouse.buttons&~buttons;buttons=mouse.buttons;
    if(mouse.dx||mouse.dy||clicked||mouse.wheel)pointer=true;
    if((pressed&PAD_CIRCLE)||(clicked&2)||((clicked&1)&&Inside(490,393,100,28))){quest_owner.clear();return;}
    if((pressed&PAD_SQUARE)||((clicked&1)&&Inside(42,393,330,28))){quest_filter=(quest_filter+1)%3;quest_selected=quest_detail=0;quest_list.Reset();}
    const auto entries=JournalQuests();const int count=static_cast<int>(entries.size());const int previous=quest_selected;
    if(pressed&PAD_UP)--quest_selected;
    if(pressed&PAD_DOWN)++quest_selected;
    if(pressed&PAD_LEFT)--quest_detail;
    if(pressed&PAD_RIGHT)++quest_detail;
    if(mouse.wheel){if(Inside(290,95,305,275))quest_detail+=mouse.wheel<0?1:-1;else quest_selected+=mouse.wheel<0?1:-1;}
    if(clicked&1){const int hit=quest_list.HitTest(px,py,count);if(hit>=0)quest_selected=hit;
        else if(Inside(300,365,100,25))--quest_detail;else if(Inside(480,365,100,25))++quest_detail;}
    quest_selected=std::clamp(quest_selected,0,std::max(0,count-1));quest_list.SetSelectedIndex(quest_selected,count);
    if(previous!=quest_selected)quest_detail=0;quest_detail=std::max(0,quest_detail);
}

void DrawQuests() {
    UI::Divider::DrawVertical(282,148,348);
    Text(0,"Mod Quests",42,94,540,true);
    const char *filters[]={"All","In progress","Completed"};
    Text(1,Literal(quest_owner)+" - "+filters[quest_filter],42,120,550);
    const auto entries=JournalQuests();const int count=static_cast<int>(entries.size());
    quest_selected=std::clamp(quest_selected,0,std::max(0,count-1));quest_list.SetSelectedIndex(quest_selected,count);
    quest_list.Draw(count,[&](int i,const UI::Rect &rect,bool selected_row,int){
        Text(10+i-quest_list.FirstRow(),std::string(entries[i].progress>=entries[i].goal?"[+] ":"[ ] ")+Literal(entries[i].title),rect.x,rect.y,rect.w,selected_row);
    });
    std::string body="No quests match this filter.";
    if(count){const auto &q=entries[quest_selected];body=q.id+"\n\n"+(q.progress>=q.goal?"Completed":"In progress")+"\n"+std::to_string(q.progress)+" / "+std::to_string(q.goal)+"\n\n"+q.description;}
    const auto lines=Lines(body,298);const int pages=std::max(1,(static_cast<int>(lines.size())+8)/9);
    quest_detail=std::clamp(quest_detail,0,pages-1);
    for(int i=0;i<9&&quest_detail*9+i<static_cast<int>(lines.size());++i)Text(40+i,Literal(lines[quest_detail*9+i]),295,150+i*21,300);
    Text(60,"<  Details "+std::to_string(quest_detail+1)+"/"+std::to_string(pages)+"  >",300,365,285);
    Text(61,std::string("{square} Filter: ")+filters[quest_filter],42,395,330);
    Text(63,"{circle} Back",490,395,110);
    if(pointer){cursor.SetPointer(px,py);cursor.Draw();}
}

void OpenActions() {
    action_owner = SelectedActionOwner();
    action_selected = action_detail = 0;
    action_error.clear();
    action_list.Reset();
}

void RunActions(std::uint16_t pressed) {
    const auto actions = Scripts().Actions(action_owner);
    const int  count = static_cast<int>(actions.size());
    if (pressed) {
        pointer = false;
    }
    const auto mouse = InputTakeMenuMouse();
    int        w = 640, h = 480;
    WindowSize(w, h);
    const float scale = 1.0f / std::max(0.1f, std::min(w / 640.0f, h / 480.0f));
    px = std::clamp(px + mouse.dx * scale, 0.0f, 639.0f);
    py = std::clamp(py + mouse.dy * scale, 0.0f, 479.0f);
    const auto clicked = mouse.buttons & ~buttons;
    buttons = mouse.buttons;
    if (mouse.dx || mouse.dy || clicked || mouse.wheel) {
        pointer = true;
    }
    if ((pressed & PAD_CIRCLE) || (clicked & 2) || ((clicked & 1) && Inside(490, 393, 100, 28))) {
        action_owner.clear();
        return;
    }
    const int previous = action_selected;
    if (pressed & PAD_UP) {
        --action_selected;
    }
    if (pressed & PAD_DOWN) {
        ++action_selected;
    }
    if (pressed & PAD_LEFT) {
        --action_detail;
    }
    if (pressed & PAD_RIGHT) {
        ++action_detail;
    }
    if (mouse.wheel) {
        if (Inside(290, 95, 305, 275)) {
            action_detail += mouse.wheel < 0 ? 1 : -1;
        } else {
            action_selected += mouse.wheel < 0 ? 1 : -1;
        }
    }
    if (clicked & 1) {
        const int hit = action_list.HitTest(px, py, count);
        if (hit >= 0) {
            action_selected = hit;
        } else if (Inside(300, 365, 100, 25)) {
            --action_detail;
        } else if (Inside(480, 365, 100, 25)) {
            ++action_detail;
        }
    }
    action_selected = std::clamp(action_selected, 0, std::max(0, count - 1));
    action_list.SetSelectedIndex(action_selected, count);
    if (previous != action_selected) {
        action_detail = 0;
        action_error.clear();
    }
    action_detail = std::max(0, action_detail);
    if (count && ((pressed & PAD_CROSS) || ((clicked & 1) && Inside(42, 393, 160, 28)))) {
        Scripts().QueueAction(action_owner, actions[action_selected].id, action_error);
    }
}

void DrawActions() {
    UI::Divider::DrawVertical(282, 148, 348);
    Text(0, "Mod Actions", 42, 94, 540, true);
    Text(1, Literal(action_owner) + " - current run", 42, 120, 550);
    const auto actions = Scripts().Actions(action_owner);
    const int  count = static_cast<int>(actions.size());
    action_selected = std::clamp(action_selected, 0, std::max(0, count - 1));
    action_list.SetSelectedIndex(action_selected, count);
    action_list.Draw(count, [&](int i, const UI::Rect &rect, bool selected_row, int) {
        Text(10 + i - action_list.FirstRow(), Literal(actions[i].title), rect.x, rect.y, rect.w, selected_row);
    });
    std::string body = "No actions available in this run.";
    if (count) {
        const auto &a = actions[action_selected];
        body = a.id;
        if (!a.result.empty()) {
            body += "\n\n" + a.result;
        }
        body += std::string("\n\n") + (a.available ? "Ready. Runs on the next gameplay tick." : "Unavailable: gameplay or package stopped.");
        body += "\n\n" + a.description;
    }
    if (!action_error.empty()) {
        body = action_error + "\n\n" + body;
    }
    const auto lines = Lines(body, 298);
    const int  pages = std::max(1, (static_cast<int>(lines.size()) + 8) / 9);
    action_detail = std::clamp(action_detail, 0, pages - 1);
    for (int i = 0; i < 9 && action_detail * 9 + i < static_cast<int>(lines.size()); ++i) {
        Text(40 + i, Literal(lines[action_detail * 9 + i]), 295, 150 + i * 21, 300);
    }
    Text(60, "<  Details " + std::to_string(action_detail + 1) + "/" + std::to_string(pages) + "  >", 300, 365, 285);
    Text(61, "{cross} Run", 42, 395, 160);
    Text(63, "{circle} Back", 490, 395, 110);
    if (pointer) {
        cursor.SetPointer(px, py);
        cursor.Draw();
    }
}
} // namespace

void Open() {
    Shared().Refresh();
    open = true;
    pointer = false;
    selected = first = detail = 0;
    list.Reset();
    buttons = InputTakeMenuMouse().buttons;
    trust_id.clear();
    action_owner.clear();
    quest_owner.clear();
}

void Close() {
    open = false;
    action_owner.clear();
    quest_owner.clear();
}

bool IsOpen() { return open; }

void Run(std::uint16_t pressed) {
    if (!quest_owner.empty()) { RunQuests(pressed); return; }
    if (!action_owner.empty()) {
        RunActions(pressed);
        return;
    }
    if (!trust_id.empty()) {
        const auto mouse = InputTakeMenuMouse();
        int        w = 640, h = 480;
        WindowSize(w, h);
        const float scale = 1.0f / std::max(0.1f, std::min(w / 640.0f, h / 480.0f));
        px = std::clamp(px + mouse.dx * scale, 0.0f, 639.0f);
        py = std::clamp(py + mouse.dy * scale, 0.0f, 479.0f);
        const auto clicked = mouse.buttons & ~buttons;
        buttons = mouse.buttons;
        if (mouse.dx || mouse.dy || clicked) {
            pointer = true;
        }
        if ((pressed & PAD_CIRCLE) || (clicked & 2) || ((clicked & 1) && Inside(410, 393, 180, 28))) {
            trust_id.clear();
        } else if ((pressed & PAD_CROSS) || ((clicked & 1) && Inside(42, 393, 240, 28))) {
            Shared().SetTrusted(trust_id, trust_fingerprint, trust_value);
            trust_id.clear();
        }
        return;
    }
    if (pressed) {
        pointer = false;
    }
    if (pressed & PAD_CIRCLE) {
        Close();
        return;
    }
    if (pressed & PAD_DOWN) {
        ++selected;
        detail = 0;
    }
    if (pressed & PAD_UP) {
        --selected;
        detail = 0;
    }
    if (pressed & PAD_LEFT) {
        detail = std::max(0, detail - 1);
    }
    if (pressed & PAD_RIGHT) {
        ++detail;
    }
    if (pressed & PAD_SQUARE) {
        Shared().Refresh();
        detail = 0;
    }
    Clamp();
    if (pressed & PAD_CROSS) {
        Toggle();
    }
    if (pressed & PAD_TRIANGLE) {
        ReviewTrust();
    }
    if (pressed & PAD_L1) { OpenQuests(); }
    else if (pressed & PAD_R1) {
        OpenActions();
    }
    const auto mouse = InputTakeMenuMouse();
    if (mouse.dx != 0 || mouse.dy != 0 || mouse.buttons != buttons) {
        pointer = true;
    }
    int w = 640, h = 480;
    WindowSize(w, h);
    const float scale = 1.0f / std::max(0.1f, std::min(w / 640.0f, h / 480.0f));
    px = std::clamp(px + mouse.dx * scale, 0.0f, 639.0f);
    py = std::clamp(py + mouse.dy * scale, 0.0f, 479.0f);
    const auto clicked = mouse.buttons & ~buttons;
    buttons = mouse.buttons;
    if (mouse.wheel != 0) {
        if (Inside(290, 95, 305, 275)) {
            detail = std::max(0, detail + (mouse.wheel < 0 ? 1 : -1));
        } else {
            selected += mouse.wheel < 0 ? 1 : -1;
            detail = 0;
        }
    }
    if (clicked & 1) {
        const int hit = list.HitTest(px, py, static_cast<int>(Shared().Preview().Packages().size()));
        if (hit >= 0) {
            selected = hit;
            detail = 0;
        } else if (Inside(40, 393, 135, 28)) {
            Toggle();
        } else if (Inside(180, 393, 135, 28)) {
            Shared().Refresh();
            detail = 0;
        } else if (Inside(320, 393, 150, 28)) {
            ReviewTrust();
        } else if (Inside(490, 393, 100, 28)) {
            Close();
        } else if (Inside(42, 365, 112, 25)) {
            OpenQuests();
        } else if (Inside(165, 365, 112, 25)) {
            OpenActions();
        } else if (Inside(300, 365, 100, 25)) {
            detail = std::max(0, detail - 1);
        } else if (Inside(480, 365, 100, 25)) {
            ++detail;
        }
    }
    if (clicked & 2) {
        Close();
    }
    Clamp();
}

void Draw() {
    UI::Window::Draw(25, 85, 590, 351, 0x80, UI::WindowPreset::BlueStone);
    if (!quest_owner.empty()) { DrawQuests(); return; }
    if (!action_owner.empty()) {
        DrawActions();
        return;
    }
    if (!trust_id.empty()) {
        Text(0, trust_value ? "Trust native plugin?" : "Revoke native trust?", 42, 94, 550, true);
        const auto lines = Lines("Package: " + trust_id + "\nNative code runs with the game's access to your computer. It is not sandboxed. Trust only code you have reviewed or obtained from a trusted author.\nPermissions: " + trust_permissions + "\nChanges require restart; enabling remains separate.", 540);
        for (int i = 0; i < std::min(11, static_cast<int>(lines.size())); ++i) {
            Text(40 + i, Literal(lines[i]), 42, 130 + i * 22, 540);
        }
        Text(61, "{cross} Confirm", 42, 395, 230);
        Text(63, "{circle} Cancel", 410, 395, 180);
        if (pointer) {
            cursor.SetPointer(px, py);
            cursor.Draw();
        }
        return;
    }
    UI::Divider::DrawVertical(282, 148, 348);
    Text(0, "Chronicle Mod Manager", 42, 94, 540, true);
    Text(1, Shared().Pending() ? "Changes saved. Restart to apply." : "Select a mod. Changes apply after restart.", 42, 120, 550);
    const auto &packages = Shared().Preview().Packages();
    if (packages.empty()) {
        Text(2, "No packages found.", 42, 150);
        Text(3, "Install packages in save/mods or mods beside the game.", 42, 182, 545);
    }
    list.Draw(static_cast<int>(packages.size()), [&](int i, const UI::Rect &rect, bool selected_row, int) {
        const auto &p = packages[i];
        Text(10 + i - first, std::string(p.enabled ? "[ON] " : "[OFF] ") + Literal(p.name.empty() ? p.root.filename().string() : p.name), rect.x, rect.y, rect.w, selected_row);
    });
    std::string body;
    if (selected < static_cast<int>(packages.size())) {
        const auto &p = packages[selected];
        body = p.id + "  " + p.version + "\nNext run: " + (!p.problems.empty() ? "BLOCKED" : p.enabled ? "ENABLED"
                                                                                                       : "DISABLED");
        bool active = false;
        for (const auto &a : Shared().Active().Packages()) {
            if (a.root == p.root) {
                active = a.Active();
            }
        }
        body += std::string("\nThis run: ") + (active ? "ACTIVE" : "INACTIVE");
        if (!p.native_library.empty()) {
            body += std::string("\nNative plugin: ") + (p.native_trusted ? "TRUSTED" : "NOT TRUSTED") + "\nTriangle: review or revoke trust.";
        }
        for (const auto &permission : p.permissions) {
            body += "\nPermission: " + permission;
        }
        for (const auto &dependency : p.dependencies) {
            body += "\nRequires: " + dependency;
            if (const auto rule = p.dependency_versions.find(dependency); rule != p.dependency_versions.end()) {
                body += " " + rule->second.Describe();
            }
        }
        for (const auto &problem : p.problems) {
            body += "\n\n" + problem;
        }
        if (p.problems.empty()) {
            body += "\n\nNo detected conflicts.";
        }
        for (const auto &q : Scripts().Quests()) {
            if (q.owner == p.id) {
                body += "\n\nQuest: " + q.title + " " + std::to_string(q.progress) + "/" + std::to_string(q.goal) + "\n" + q.description;
            }
        }
        for (const auto &message : Scripts().Diagnostics()) {
            if (message.starts_with(p.id + ":")) {
                body += "\n\nModule diagnostic: " + message;
            }
        }
        for (const auto &message : Scripts().Logs()) {
            if (message.starts_with(p.id + ":")) {
                body += "\n\nLog: " + message;
            }
        }
        body += "\n\nFolder: " + p.root.generic_string();
    }
    for (const auto &error : Shared().Preview().Errors()) {
        body += "\n" + error;
    }
    if (!Shared().Error().empty()) {
        body = Shared().Error() + "\n\n" + body;
    }
    auto          lines = Lines(body, 298);
    constexpr int page_lines = 9;
    int           pages = std::max(1, (static_cast<int>(lines.size()) + page_lines - 1) / page_lines);
    detail = std::clamp(detail, 0, pages - 1);
    for (int i = 0; i < page_lines && detail * page_lines + i < static_cast<int>(lines.size()); ++i) {
        Text(40 + i, Literal(lines[detail * page_lines + i]), 295, 150 + i * 21, 300);
    }
    Text(60, "<  Details " + std::to_string(detail + 1) + "/" + std::to_string(pages) + "  >", 300, 365, 285);
    if (!SelectedQuestOwner().empty()) Text(66, "{L1} Quests", 42, 365, 112);
    if (!SelectedActionOwner().empty()) {
        Text(65, "{R1} Actions", 165, 365, 112);
    }
    Text(61, "{cross} Toggle", 42, 395, 135);
    Text(62, "{square} Rescan", 180, 395, 135);
    Text(64, "{triangle} Trust", 320, 395, 150);
    Text(63, "{circle} Back", 490, 395, 110);
    if (pointer) {
        cursor.SetPointer(px, py);
        cursor.Draw();
    }
}
} // namespace mods::menu
