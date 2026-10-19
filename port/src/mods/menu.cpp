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
} // namespace

void Open() {
    Shared().Refresh();
    open = true;
    pointer = false;
    selected = first = detail = 0;
    list.Reset();
    buttons = InputTakeMenuMouse().buttons;
    trust_id.clear();
}

void Close() { open = false; }

bool IsOpen() { return open; }

void Run(std::uint16_t pressed) {
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
                body += "\n\nScript error: " + message;
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
