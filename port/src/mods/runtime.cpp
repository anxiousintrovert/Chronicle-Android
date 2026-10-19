#include "runtime.hpp"

#include <nlohmann/json.hpp>
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

namespace mods {
using Json = nlohmann::json;

namespace {
constexpr std::size_t memory_limit = 8 * 1024 * 1024;

struct Module {
    Package      package;
    lua_State   *lua = nullptr;
    std::size_t  memory = 0;
    int          remaining = 0, pending_money = 0;
    int          reference = LUA_NOREF;
    bool         failed = false, playing = false;
    Json        *state = nullptr;
    const World *world = nullptr;
    const char  *callback = nullptr;

    ~Module() {
        if (lua) {
            lua_close(lua);
        }
    }
};

void *Allocate(void *user, void *ptr, std::size_t old_size, std::size_t size) {
    auto &m = *static_cast<Module *>(user);
    if (!ptr) {
        old_size = 0;
    }
    if (!size) {
        m.memory -= old_size;
        std::free(ptr);
        return nullptr;
    }
    if (size > memory_limit || m.memory - old_size > memory_limit - size) {
        return nullptr;
    }
    auto *result = std::realloc(ptr, size);
    if (result) {
        m.memory = m.memory - old_size + size;
    }
    return result;
}

Module &Self(lua_State *L) { return **static_cast<Module **>(lua_getextraspace(L)); }

void Budget(lua_State *L, lua_Debug *) {
    if (--Self(L).remaining <= 0) {
        luaL_error(L, "mod instruction budget exceeded");
    }
}

int Failure(lua_State *L, const char *reason) {
    lua_pushnil(L);
    lua_pushstring(L, reason);
    return 2;
}

bool Key(const char *key) {
    if (!key || !*key || std::strlen(key) > 64) {
        return false;
    }
    for (; *key; ++key) {
        if (!((*key >= 'a' && *key <= 'z') || (*key >= '0' && *key <= '9') || *key == '_' || *key == '.')) {
            return false;
        }
    }
    return true;
}

int Get(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    if (!Key(key)) {
        return Failure(L, "invalid state key");
    }
    auto &state = *Self(L).state;
    if (!state.contains("values") || !state["values"].contains(key)) {
        lua_pushnil(L);
        return 1;
    }
    const auto &value = state["values"][key];
    if (value.is_boolean()) {
        lua_pushboolean(L, value.get<bool>());
    } else if (value.is_number()) {
        lua_pushnumber(L, value.get<double>());
    } else if (value.is_string()) {
        lua_pushstring(L, value.get_ref<const std::string &>().c_str());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int Set(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    if (!Key(key)) {
        return Failure(L, "invalid state key");
    }
    auto &values = (*Self(L).state)["values"];
    if (values.is_null()) {
        values = Json::object();
    }
    if (!values.contains(key) && values.size() >= 128) {
        return Failure(L, "state key limit reached");
    }
    switch (lua_type(L, 2)) {
        case LUA_TNIL:
            values.erase(key);
            break;
        case LUA_TBOOLEAN:
            values[key] = lua_toboolean(L, 2) != 0;
            break;
        case LUA_TNUMBER:
            if (!std::isfinite(lua_tonumber(L, 2))) {
                return Failure(L, "state numbers must be finite");
            }
            values[key] = lua_tonumber(L, 2);
            break;
        case LUA_TSTRING:
            if (lua_rawlen(L, 2) > 256) {
                return Failure(L, "state string limit is 256 bytes");
            }
            values[key] = lua_tostring(L, 2);
            break;
        default:
            return Failure(L, "state values must be scalar");
    }
    lua_pushboolean(L, true);
    return 1;
}

int QuestAdd(lua_State *L) {
    const char *id = luaL_checkstring(L, 1);
    const auto  amount = luaL_checkinteger(L, 2);
    if (!Self(L).playing || amount < 0 || amount > 1000000) {
        return Failure(L, "quest updates require gameplay and a bounded nonnegative amount");
    }
    for (const auto &q : Self(L).package.quests) {
        if (q.id == id) {
            auto &quests = (*Self(L).state)["quests"];
            if (quests.is_null()) {
                quests = Json::object();
            }
            const int progress = quests.value(q.id, 0);
            quests[q.id] = std::min<long long>(q.goal, progress + amount);
            lua_pushinteger(L, quests[q.id].get<int>());
            return 1;
        }
    }
    return Failure(L, "quest must be declared by this mod");
}

int QuestGet(lua_State *L) {
    const char *id = luaL_checkstring(L, 1);
    for (const auto &q : Self(L).package.quests) {
        if (q.id == id) {
            const auto &state = *Self(L).state;
            lua_pushinteger(L, state.contains("quests") ? state["quests"].value(q.id, 0) : 0);
            return 1;
        }
    }
    return Failure(L, "unknown quest");
}

int AddMoney(lua_State *L) {
    const auto amount = luaL_checkinteger(L, 1);
    auto      &m = Self(L);
    if (!m.playing || std::ranges::find(m.package.permissions, "economy.write") == m.package.permissions.end()) {
        return Failure(L, "economy.write permission and active gameplay required");
    }
    if (amount < -32767 || amount > 32767 || std::abs(m.pending_money + amount) > 32767) {
        return Failure(L, "currency delta exceeds callback limit");
    }
    m.pending_money += static_cast<int>(amount);
    lua_pushboolean(L, true);
    return 1;
}

int HasCapability(lua_State *L) {
    lua_pushboolean(L, Supports(luaL_checkstring(L, 1)));
    return 1;
}

// Every call is protected. No filesystem, process, native-module, debug, coroutine,
// bytecode-loader, or protected-call functions are exposed to mod scripts.
int Bootstrap(lua_State *L) {
    for (const auto &lib : {
             std::pair{LUA_GNAME,       luaopen_base  },
             {LUA_TABLIBNAME,  luaopen_table },
             {LUA_STRLIBNAME,  luaopen_string},
             {LUA_MATHLIBNAME, luaopen_math  },
             {LUA_UTF8LIBNAME, luaopen_utf8  }
    }) {
        luaL_requiref(L, lib.first, lib.second, 1);
        lua_pop(L, 1);
    }
    for (const char *name : {"dofile", "loadfile", "load", "collectgarbage", "pcall", "xpcall", "print", "setmetatable", "getmetatable"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_newtable(L);
    const luaL_Reg api[] = {
        {"get",       Get          },
        {"set",       Set          },
        {"quest_add", QuestAdd     },
        {"quest_get", QuestGet     },
        {"add_money", AddMoney     },
        {"supports",  HasCapability},
        {nullptr,     nullptr      }
    };
    luaL_setfuncs(L, api, 0);
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "api_version");
    lua_setglobal(L, "chronicle");
    return 0;
}

int Dispatch(lua_State *L) {
    auto &m = Self(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, m.reference);
    lua_getfield(L, -1, m.callback);
    if (lua_isnil(L, -1)) {
        return 0;
    }
    if (!lua_isfunction(L, -1)) {
        return luaL_error(L, "callback must be a function");
    }
    lua_newtable(L);
    lua_pushinteger(L, m.world->mode);
    lua_setfield(L, -2, "mode");
    lua_pushinteger(L, m.world->map);
    lua_setfield(L, -2, "map");
    lua_pushinteger(L, m.world->money);
    lua_setfield(L, -2, "money");
    lua_pushboolean(L, m.world->playing);
    lua_setfield(L, -2, "playing");
    lua_call(L, 1, 0);
    return 0;
}

int Reference(lua_State *L) {
    if (!lua_istable(L, 1)) {
        return luaL_error(L, "script must return a callback table");
    }
    Self(L).reference = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}
} // namespace

struct Runtime::Impl {
    std::vector<std::unique_ptr<Module>> modules;
    Json                                 state = Json::object();
    std::vector<std::string>             diagnostics;
    World                                previous;
    bool                                 observed = false;
    Manager                              plan;

    ~Impl() { modules.clear(); } // Lua states close before the storage their API references.

    bool Check(Module &m, int status) {
        if (status == LUA_OK) {
            return true;
        }
        const char *error = lua_type(m.lua, -1) == LUA_TSTRING ? lua_tostring(m.lua, -1) : "non-text Lua error";
        diagnostics.push_back(m.package.id + ": " + std::string(error ? error : "Lua error").substr(0, 1024));
        m.failed = true;
        m.pending_money = 0;
        lua_settop(m.lua, 0);
        return false;
    }

    void ResetBudget(Module &m) {
        m.remaining = 100;
        lua_sethook(m.lua, Budget, LUA_MASKCOUNT, 1000);
    }

    void Call(Module &m, const char *name, const World &world, const std::function<bool(int)> &money) {
        if (m.failed || !m.lua) {
            return;
        }
        auto      *L = m.lua;
        const Json before = *m.state;
        m.pending_money = 0;
        m.playing = world.playing;
        m.world = &world;
        m.callback = name;
        lua_pushcfunction(L, Dispatch);
        ResetBudget(m);
        if (!Check(m, lua_pcall(L, 0, 0, 0))) {
            *m.state = before;
            return;
        }
        if (m.pending_money && (!money || !money(m.pending_money))) {
            *m.state = before;
            m.failed = true;
            diagnostics.push_back(m.package.id + ": currency operation was rejected; script stopped");
        }
        m.pending_money = 0;
    }
};

Runtime::Runtime() : impl_(std::make_unique<Impl>()) {}

Runtime::~Runtime() = default;

void Runtime::Start(const Manager &manager) {
    impl_->plan = manager;
    impl_->modules.clear();
    impl_->diagnostics.clear();
    impl_->observed = false;
    for (const auto &id : manager.LoadOrder()) {
        const auto found = std::ranges::find(manager.Packages(), id, &Package::id);
        if (found == manager.Packages().end()) {
            continue;
        }
        if (found->script.empty() && found->quests.empty()) {
            continue;
        }
        auto  module = std::make_unique<Module>();
        auto &m = *module;
        m.package = *found;
        if (!impl_->state.contains(id)) {
            impl_->state[id] = Json::object();
        }
        m.state = &impl_->state[id];
        for (const auto &dependency : m.package.dependencies) {
            for (const auto &loaded : impl_->modules) {
                if (loaded->package.id == dependency && loaded->failed) {
                    m.failed = true;
                    impl_->diagnostics.push_back(id + ": dependency script failed: " + dependency);
                }
            }
        }
        if (!m.failed && !m.package.script.empty()) {
            m.lua = lua_newstate(Allocate, &m, 0);
            if (!m.lua) {
                impl_->diagnostics.push_back(id + ": could not create Lua state");
                m.failed = true;
            } else {
                *static_cast<Module **>(lua_getextraspace(m.lua)) = &m;
                lua_pushcfunction(m.lua, Bootstrap);
                if (impl_->Check(m, lua_pcall(m.lua, 0, 0, 0))) {
                    std::error_code file_error;
                    const auto      size = std::filesystem::file_size(m.package.script, file_error);
                    if (file_error || size > 1024 * 1024) {
                        m.failed = true;
                        impl_->diagnostics.push_back(id + ": script missing or exceeds 1 MiB");
                        impl_->modules.push_back(std::move(module));
                        continue;
                    }
                    std::ifstream     input(m.package.script, std::ios::binary);
                    const std::string source((std::istreambuf_iterator<char>(input)), {});
                    impl_->ResetBudget(m);
                    if (impl_->Check(m, luaL_loadbufferx(m.lua, source.data(), source.size(), id.c_str(), "t")) && impl_->Check(m, lua_pcall(m.lua, 0, 1, 0))) {
                        lua_pushcfunction(m.lua, Reference);
                        lua_insert(m.lua, -2);
                        impl_->Check(m, lua_pcall(m.lua, 1, 0, 0));
                    }
                }
            }
        }
        impl_->modules.push_back(std::move(module));
    }
}

void Runtime::Tick(const World &world, const std::function<bool(int)> &money) {
    const bool entered = world.playing && (!impl_->observed || world.map != impl_->previous.map);
    for (auto &ptr : impl_->modules) {
        auto &m = *ptr;
        if (m.failed) {
            continue;
        }
        for (const auto &dependency : m.package.dependencies) {
            for (const auto &other : impl_->modules) {
                if (other->package.id == dependency && other->failed) {
                    m.failed = true;
                    impl_->diagnostics.push_back(m.package.id + ": dependency script failed: " + dependency);
                }
            }
        }
        if (m.failed) {
            continue;
        }
        if (entered) {
            for (const auto &q : m.package.quests) {
                if (q.map == world.map && m.state->value("last_map", -1) != world.map) {
                    auto &values = (*m.state)["quests"];
                    if (values.is_null()) {
                        values = Json::object();
                    }
                    values[q.id] = std::min(q.goal, values.value(q.id, 0) + 1);
                }
            }
            (*m.state)["last_map"] = world.map;
            impl_->Call(m, "on_map_enter", world, money);
        }
        if (world.playing) {
            impl_->Call(m, "on_tick", world, money);
        }
    }
    if (world.playing) {
        impl_->previous = world;
        impl_->observed = true;
    }
}

void Runtime::ResetState() {
    impl_->modules.clear();
    impl_->state = Json::object();
    Start(impl_->plan);
}

std::string Runtime::SaveState() const {
    if (impl_->state.empty()) {
        return {};
    }
    Json required = Json::object();
    for (const auto &m : impl_->modules) {
        required[m->package.id] = m->package.version;
    }
    return Json{
        {"schema_version", 1           },
        {"mods",           impl_->state},
        {"required",       required    }
    }
        .dump();
}

bool Runtime::LoadState(const std::string &text, std::string &error) {
    if (text.empty()) {
        ResetState();
        return true;
    }
    try {
        if (text.size() > 4 * 1024 * 1024) {
            throw std::runtime_error("mod state exceeds 4 MiB");
        }
        const auto json = Json::parse(text);
        if (json.at("schema_version") != 1 || !json.at("mods").is_object() || json["mods"].size() > 1024) {
            throw std::runtime_error("invalid mod state format");
        }
        if (!json.at("required").is_object()) {
            throw std::runtime_error("missing gameplay mod requirements");
        }
        for (const auto &[id, version] : json["required"].items()) {
            if (!version.is_string()) {
                throw std::runtime_error("invalid required mod version");
            }
            const auto version_text = version.get<std::string>();
            const auto found = std::ranges::find_if(impl_->modules, [&](const auto &m) { return m->package.id == id && m->package.version == version_text; });
            if (found == impl_->modules.end()) {
                throw std::runtime_error("save requires enabled, working mod " + id + " at version " + version.dump());
            }
        }
        for (const auto &[id, state] : json["mods"].items()) {
            if (!state.is_object()) {
                throw std::runtime_error("invalid mod state");
            }
            if (state.contains("last_map") && (!state["last_map"].is_number_integer() || state["last_map"] < -1 || state["last_map"] > 999)) {
                throw std::runtime_error("invalid saved map");
            }
            if (state.contains("values")) {
                if (!state["values"].is_object() || state["values"].size() > 128) {
                    throw std::runtime_error("invalid state values");
                }
                for (const auto &[key, value] : state["values"].items()) {
                    if (!Key(key.c_str()) || !(value.is_boolean() || value.is_number() || (value.is_string() && value.get_ref<const std::string &>().size() <= 256))) {
                        throw std::runtime_error("invalid state value");
                    }
                }
            }
            if (state.contains("quests")) {
                if (!state["quests"].is_object() || state["quests"].size() > 64) {
                    throw std::runtime_error("invalid quest state");
                }
                for (const auto &[key, value] : state["quests"].items()) {
                    if (!value.is_number_integer() || value < 0 || value > 1000000) {
                        throw std::runtime_error("invalid quest progress");
                    }
                }
            }
        }
        Runtime staged;
        staged.Start(impl_->plan);
        if (!staged.impl_->diagnostics.empty()) {
            throw std::runtime_error("cannot initialize saved mods: " + staged.impl_->diagnostics.front());
        }
        staged.impl_->state = json["mods"];
        for (auto &m : staged.impl_->modules) {
            if (!staged.impl_->state.contains(m->package.id)) {
                staged.impl_->state[m->package.id] = Json::object();
            }
            m->state = &staged.impl_->state[m->package.id];
        }
        impl_.swap(staged.impl_);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

std::vector<QuestStatus> Runtime::Quests() const {
    std::vector<QuestStatus> result;
    for (const auto &m : impl_->modules) {
        for (const auto &q : m->package.quests) {
            const int value = m->state->contains("quests") ? (*m->state)["quests"].value(q.id, 0) : 0;
            result.push_back({m->package.id, q.id, q.title, q.description, std::min(value, q.goal), q.goal});
        }
    }
    return result;
}

std::vector<std::string> Runtime::Diagnostics() const { return impl_->diagnostics; }

Runtime &Scripts() {
    static Runtime runtime;
    return runtime;
}
} // namespace mods
