#include "town.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "character.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dataread_port.hpp"
#include "draw3d.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "main.hpp"
#include "platform/arena.hpp"
#include "platform/memory.hpp"
#include "savedata.hpp"
#include "town_motion.hpp"
#include "town_selection.hpp"

extern s32 mode;
extern int viewMode;
extern int start_event_no, start_system_event;

namespace mods {
namespace {
TownSelection                      selection;
std::unique_ptr<TownMotionBinding> motion;
CDataAlloc2<1>                     arena(-1);
ArenaMemory                        memory;
std::uint64_t                      scene = 1;
bool                               loading = false;
constexpr std::array               packs{
    "chara/c01d.chr", "dun/mainchara/c04b.chr", "dun/mainchara/c06b.chr",
    "dun/mainchara/c05a.chr", "dun/mainchara/c10b.chr", "dun/mainchara/c18a.chr"};

void ReleaseStorage() {
    Draw3DRetireVisuals(memory.base, memory.capacity);
    ArenaMemoryUnmap(memory);
    arena.base = arena.buffer = nullptr;
    arena.used = 0;
    arena.limit = -1;
}

TownSelectionContext Context() {
    TownSelectionContext context{scene, SaveData ? int(SaveData->GetDngStatus()->party_size) : 0, {}};
    if (mode != GAME_MODE_EDIT || GameMode != ED_MODE_WALK || EdInteriorFlag || viewMode || MapNo < 0 || MapNo >= 6 ||
        start_event_no > 0 || start_system_event > 0) {
        context.unavailable = "requires outdoor town walking outside menus, events and first-person view";
    } else if (!SaveData || !pEditGround || !read_buffer || Chara != static_cast<CCharacter *>(&MainChara) ||
               !MainChara.frame || !CharaBuffer.base || CharaBuffer.limit <= 0) {
        context.unavailable = "native town character resources are unavailable";
    } else if (context.joined < 1 || context.joined > 6) {
        context.unavailable = "invalid native party size";
    } else {
        std::array<float, 4> position{}, rotation{};
        MainChara.GetPosition(position.data());
        MainChara.GetRotation(rotation.data());
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(position[i]) || !std::isfinite(rotation[i])) {
                context.unavailable = "native town character transform is invalid";
            }
        }
    }
    return context;
}

std::string Assets(int member) {
    if (member < 0 || member >= int(packs.size()) || !DataFileAvailable(packs[member])) {
        return "required retail town character pack is unavailable";
    }
    // A requested replacement must also retain a path back to retail Toan.
    if (!DataFileAvailable(packs[0])) {
        return "retail Toan restoration pack is unavailable";
    }
    return {};
}

std::string Load(int member) {
    std::array<float, 4> position{}, rotation{};
    MainChara.GetPosition(position.data());
    MainChara.GetRotation(rotation.data());
    motion.reset();
    auto *storage = member ? &arena : &CharaBuffer;
    if (member && arena.limit < 0) {
        constexpr int quads = 115000 * kArenaHeadroom;
        memory = ArenaMemoryMap(std::size_t(quads) * 16);
        arena.base = arena.buffer = memory.base;
        arena.limit = quads;
    }
    if (member) {
        Draw3DRetireVisuals(memory.base, memory.capacity);
    }
    storage->Reset();

    struct Loading {
        Loading() { loading = true; }

        ~Loading() { loading = false; }
    } guard;

    EdLoadMainChara(const_cast<char *>(packs[member]), const_cast<char *>(member ? "base.cfg" : "info.cfg"), storage);
    MainChara.SetPosition(position.data());
    MainChara.SetRotation(rotation.data());
    if (member) {
        motion = std::make_unique<TownMotionBinding>(MainChara, member);
    } else {
        ReleaseStorage();
    }
    return {};
}
} // namespace

std::string ValidateTownSelection(std::string_view op, const nlohmann::json &args) {
    if (!args.is_object()) {
        return "arguments must be an object";
    }
    if (op == "party.town_state") {
        return args.empty() ? "" : "party.town_state takes no arguments";
    }
    if (op != "party.town_set_active" || args.size() != 1 || !args.contains("character") ||
        !args["character"].is_number_integer() || args["character"].get<double>() < 0 || args["character"].get<double>() > 5) {
        return "provide only character, an integer from 0 to 5";
    }
    return {};
}

nlohmann::json ReadTownSelection() {
    const auto context = Context();
    auto       state = selection.State();
    state["ready"] = context.unavailable.empty() && context.joined >= 1 && context.joined <= 6 &&
                     state["pending_owner"] == "" && !state["restore_pending"].get<bool>();
    state["unavailable"] = context.unavailable;
    state["joined"] = std::clamp(context.joined, 0, 6);
    state["storage_bytes"] = memory.capacity;
    return state;
}

bool QueueTownSelection(const std::vector<GameCommand> &commands, int money, std::string &error) {
    return selection.Queue(Context(), Scripts().Epoch(), commands, money, Assets, error);
}

void FinishTownSelection() {
    if (!selection.Engaged()) {
        return;
    }
    const auto owners = Scripts().ActiveOwners();
    selection.Finish(Context(), Scripts().Epoch(), owners, Assets, Load);
}

void RetireTownScene() {
    if (!selection.Engaged() && !motion) {
        return;
    }
    motion.reset();
    selection.SceneRetired();
    ReleaseStorage();
    ++scene;
}

void TownBodyLoaded() {
    if (!loading) {
        RetireTownScene();
    }
}
} // namespace mods
