#include "runtime.hpp"

#include <nlohmann/json.hpp>

#include "api.hpp"
#include "arguments.hpp"
#include "native.hpp"
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
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace mods {
using Json = nlohmann::json;

namespace {
constexpr std::size_t memory_limit = 8 * 1024 * 1024;

struct Action {
    std::string title, description, result;
    int         reference = LUA_NOREF;
    bool        native = false;
};

struct CustomEvent {
    std::string owner;
    Json        arguments;
    World       context;
};

struct Module {
    Package                                                                                        package;
    lua_State                                                                                     *lua = nullptr;
    std::size_t                                                                                    memory = 0;
    int                                                                                            remaining = 0, pending_money = 0;
    int                                                                                            reference = LUA_NOREF;
    int                                                                                            action_reference = LUA_NOREF;
    std::map<std::string, Action>                                                                  actions;
    bool                                                                                           failed = false, playing = false;
    Json                                                                                          *state = nullptr;
    const World                                                                                   *world = nullptr;
    const char                                                                                    *callback = nullptr;
    const ReadGame                                                                                *read_game = nullptr;
    const QueryGame                                                                               *query_game = nullptr;
    const CommitGame                                                                              *commit_game = nullptr;
    bool                                                                                           initializing = true, read_only = false;
    unsigned                                                                                       service_depth = 0;
    int                                                                                            services_left = 16;
    int                                                                                           *service_budget = &services_left;
    int                                                                                            service_reference = LUA_NOREF;
    Json                                                                                           service_arguments, service_result;
    std::map<std::string, std::vector<int>>                                                        listeners;
    unsigned                                                                                       listener_count = 0;
    std::map<std::string, int>                                                                     exports;
    std::function<bool(Module &, const char *, const char *, const Json &, Json &, std::string &)> invoke;
    Json                                                                                           arguments;
    std::string                                                                                    call_error;
    std::vector<GameCommand>                                                                       commands;
    std::vector<Json>                                                                              emissions;
    std::unique_ptr<NativeLibrary>                                                                 native;
    std::string                                                                                    native_result;
    unsigned                                                                                       native_calls = 0;
    bool                                                                                           native_active = false;
    Json                                                                                           snapshot;
    std::map<std::string, std::string>                                                             resources;
    std::vector<std::string>                                                                      *logs = nullptr;

    ~Module() {
        playing = false;
        native.reset();
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
    if (Self(L).read_only) {
        return Failure(L, "query exports cannot write owned or game state");
    }
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
    if (Self(L).read_only) {
        return Failure(L, "query exports cannot write owned or game state");
    }
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
    if (Self(L).read_only) {
        return Failure(L, "query exports cannot write owned or game state");
    }
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

bool Permitted(const Module &m, std::string_view permission) {
    return std::ranges::find(m.package.permissions, permission) != m.package.permissions.end();
}

void PushJson(lua_State *L, const Json &value) {
    if (value.is_object()) {
        lua_newtable(L);
        for (const auto &[key, child] : value.items()) {
            PushJson(L, child);
            lua_setfield(L, -2, key.c_str());
        }
    } else if (value.is_array()) {
        lua_newtable(L);
        for (std::size_t i = 0; i < value.size(); ++i) {
            PushJson(L, value[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
    } else if (value.is_boolean()) {
        lua_pushboolean(L, value.get<bool>());
    } else if (value.is_number_integer() &&
               (!value.is_number_unsigned() || value.get<uint64_t>() <= static_cast<uint64_t>(std::numeric_limits<lua_Integer>::max()))) {
        lua_pushinteger(L, value.get<lua_Integer>());
    } else if (value.is_number()) {
        lua_pushnumber(L, value.get<double>());
    } else if (value.is_string()) {
        const auto &text = value.get_ref<const std::string &>();
        lua_pushlstring(L, text.data(), text.size());
    } else {
        lua_pushnil(L);
    }
}

int ReadSnapshot(lua_State *L, const char *service) {
    auto &m = Self(L);
    if (!m.playing || !Permitted(m, "game.read") || !m.read_game || !*m.read_game) {
        return Failure(L, "game.read permission and an active gameplay provider are required");
    }
    m.snapshot = (*m.read_game)(service);
    if (m.snapshot.is_null()) {
        return Failure(L, "game snapshot is unavailable");
    }
    PushJson(L, m.snapshot);
    return 1;
}

int Player(lua_State *L) { return ReadSnapshot(L, "player"); }

int Party(lua_State *L) { return ReadSnapshot(L, "party"); }

int Inventory(lua_State *L) { return ReadSnapshot(L, "inventory"); }

int Log(lua_State *L) {
    const char *level = luaL_checkstring(L, 1);
    size_t      length = 0;
    const char *message = luaL_checklstring(L, 2, &length);
    if ((std::strcmp(level, "info") && std::strcmp(level, "warning") && std::strcmp(level, "error")) || length > 512) {
        return Failure(L, "log requires info/warning/error and at most 512 bytes");
    }
    std::string text(message, length);
    for (char &c : text) {
        if (static_cast<unsigned char>(c) < 32) {
            c = ' ';
        }
    }
    auto &m = Self(L);
    if (m.logs->size() == 128) {
        m.logs->erase(m.logs->begin());
    }
    m.logs->push_back(m.package.id + ": [" + level + "] " + text);
    lua_pushboolean(L, true);
    return 1;
}

int Resource(lua_State *L) {
    const char *id = luaL_checkstring(L, 1);
    auto       &m = Self(L);
    if (!Permitted(m, "resources.read")) {
        return Failure(L, "resources.read permission required");
    }
    // Cached at startup: no live filesystem access from callbacks.
    lua_getfield(L, LUA_REGISTRYINDEX, "chronicle.resources");
    lua_getfield(L, -1, id);
    if (lua_isnil(L, -1)) {
        return Failure(L, "unknown package resource");
    }
    return 1;
}

int HasCapability(lua_State *L) {
    lua_pushboolean(L, Supports(luaL_checkstring(L, 1)));
    return 1;
}

int ApiAvailable(lua_State *L) {
    lua_pushboolean(L, FindOperation(luaL_checkstring(L, 1)) != nullptr);
    return 1;
}

int ApiList(lua_State *L) {
    auto &m = Self(L);
    m.snapshot = Json::array();
    for (const auto &op : Operations()) {
        m.snapshot.push_back({
            {"name",        op.name       },
            {"permission",  op.permission },
            {"writes",      op.writes     },
            {"phase",       "simulation"  },
            {"description", op.description}
        });
    }
    PushJson(L, m.snapshot);
    return 1;
}

int ApiCall(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const auto *op = FindOperation(name);
    if (!op) {
        return Failure(L, "unsupported host operation");
    }
    auto &m = Self(L);
    if (!Permitted(m, op->permission)) {
        return Failure(L, "host operation permission denied");
    }
    if (!m.playing) {
        return Failure(L, "host operation requires active gameplay");
    }
    if (lua_gettop(L) > 2 || (!lua_isnoneornil(L, 2) && !lua_istable(L, 2))) {
        return Failure(L, "arguments must be a table");
    }
    if (!ReadArguments(L, 2, m.arguments, m.call_error)) {
        return Failure(L, m.call_error.c_str());
    }
    m.call_error = ValidateOperation(name, m.arguments);
    if (!m.call_error.empty()) {
        return Failure(L, m.call_error.c_str());
    }
    if (op->writes) {
        if (m.read_only) {
            return Failure(L, "query exports cannot write game state");
        }
        if (op->name == "events.emit") {
            if (m.emissions.size() >= 16) {
                return Failure(L, "at most 16 custom events per callback");
            }
            m.emissions.push_back(m.arguments);
            lua_pushboolean(L, true);
            return 1;
        }
        if (!m.commit_game || !*m.commit_game) {
            return Failure(L, "game write provider unavailable");
        }
        if (m.commands.size() >= 64) {
            return Failure(L, "callback write budget exceeded");
        }
        m.commands.push_back({name, m.arguments, m.package.id});
        lua_pushboolean(L, true); // Queued; the whole callback commits or fails together.
        return 1;
    }
    if (!m.query_game || !*m.query_game) {
        return Failure(L, "game query provider unavailable");
    }
    try {
        m.snapshot = (*m.query_game)(name, m.arguments);
    } catch (const std::exception &e) {
        m.call_error = e.what();
    }
    if (!m.call_error.empty()) {
        return Failure(L, m.call_error.c_str());
    }
    PushJson(L, m.snapshot);
    return 1;
}

int NativeAvailable(void *, const char *name) { return name && FindOperation(name); }

void NativeLog(void *context, const char *message) {
    if (!message) {
        return;
    }
    auto       &m = *static_cast<Module *>(context);
    std::string text(message, std::min<std::size_t>(std::strlen(message), 512));
    for (char &c : text) {
        if (static_cast<unsigned char>(c) < 32) {
            c = ' ';
        }
    }
    if (m.logs->size() == 128) {
        m.logs->erase(m.logs->begin());
    }
    m.logs->push_back(m.package.id + ": [native] " + text);
}

int NativeCall(void *context, const char *name, const char *arguments, const char **result) {
    auto &m = *static_cast<Module *>(context);
    if (!result) {
        return 0;
    }
    try {
        if (!name || !arguments || std::strlen(arguments) > 16384) {
            throw std::runtime_error("invalid native API arguments");
        }
        if (++m.native_calls > 128) {
            throw std::runtime_error("native callback host-call limit reached");
        }
        const auto *op = FindOperation(name);
        if (!op) {
            throw std::runtime_error("unsupported host operation");
        }
        if (!Permitted(m, op->permission)) {
            throw std::runtime_error("host operation permission denied");
        }
        if (!m.native_active || !m.playing) {
            throw std::runtime_error("host operation requires active gameplay");
        }
        const auto args = Json::parse(arguments);
        const auto transport_error = ValidateArguments(args);
        if (!transport_error.empty()) {
            throw std::runtime_error(transport_error);
        }
        const auto error = ValidateOperation(name, args);
        if (!error.empty()) {
            throw std::runtime_error(error);
        }
        if (op->writes) {
            if (op->name == "events.emit") {
                if (m.emissions.size() >= 16) {
                    throw std::runtime_error("at most 16 custom events per callback");
                }
                m.emissions.push_back(args);
                m.native_result = "{\"queued\":true}";
                *result = m.native_result.c_str();
                return 1;
            }
            if (!m.commit_game || !*m.commit_game || m.commands.size() >= 64) {
                throw std::runtime_error("write provider unavailable or callback write budget exceeded");
            }
            m.commands.push_back({name, args, m.package.id});
            m.native_result = "{\"queued\":true}";
        } else {
            if (!m.query_game || !*m.query_game) {
                throw std::runtime_error("query provider unavailable");
            }
            m.native_result = (*m.query_game)(name, args).dump();
        }
        *result = m.native_result.c_str();
        return 1;
    } catch (const std::exception &e) {
        m.native_result = Json{
            {"error", e.what()}
        }
                              .dump();
        *result = m.native_result.c_str();
        return 0;
    }
}

int NativeState(void *context, const char *key, const char *value, const char **result) {
    auto &m = *static_cast<Module *>(context);
    if (!result) {
        return 0;
    }
    try {
        if (!m.native_active) {
            throw std::runtime_error("owned state requires a native tick, event or menu callback");
        }
        if (++m.native_calls > 128) {
            throw std::runtime_error("native callback host-call limit reached");
        }
        if (!Key(key)) {
            throw std::runtime_error("invalid state key");
        }
        if (!value) {
            const auto &state = *m.state;
            m.native_result = state.contains("values") && state["values"].contains(key) ? state["values"][key].dump() : "null";
        } else {
            if (std::strlen(value) > 16384) {
                throw std::runtime_error("state JSON exceeds 16384 bytes");
            }
            const auto parsed = Json::parse(value);
            if (!(parsed.is_null() || parsed.is_boolean() || parsed.is_number() || parsed.is_string())) {
                throw std::runtime_error("state values must be scalar");
            }
            if (parsed.is_number() && !std::isfinite(parsed.get<double>())) {
                throw std::runtime_error("state numbers must be finite");
            }
            if (parsed.is_string()) {
                const auto &text = parsed.get_ref<const std::string &>();
                if (text.size() > 256 || text.find('\0') != std::string::npos) {
                    throw std::runtime_error("state strings require at most 256 UTF-8 bytes without NUL");
                }
            }
            if (parsed.is_null()) {
                if (m.state->contains("values")) {
                    (*m.state)["values"].erase(key);
                }
            } else {
                if (m.state->contains("values") && !(*m.state)["values"].contains(key) && (*m.state)["values"].size() >= 128) {
                    throw std::runtime_error("state key limit reached");
                }
                (*m.state)["values"][key] = parsed;
            }
            m.native_result = "true";
        }
        *result = m.native_result.c_str();
        return 1;
    } catch (const std::exception &e) {
        m.native_result = Json{
            {"error", e.what()}
        }
                              .dump();
        *result = m.native_result.c_str();
        return 0;
    }
}

int NativeStateGet(void *context, const char *key, const char **result) { return NativeState(context, key, nullptr, result); }

int NativeStateSet(void *context, const char *key, const char *value, const char **result) {
    // A null C pointer is invalid input, not the JSON null deletion request.
    return NativeState(context, key, value ? value : "", result);
}

int RegisterAction(lua_State *L) {
    auto       &m = Self(L);
    const char *id = luaL_checkstring(L, 1);
    const char *title = luaL_checkstring(L, 2);
    const char *description = luaL_checkstring(L, 3);
    if (!m.initializing || !Key(id) || lua_rawlen(L, 1) != std::strlen(id) || !lua_isfunction(L, 4)) {
        return Failure(L, "action requires an ID and function during initialization");
    }
    for (int i : {2, 3}) {
        const auto  size = lua_rawlen(L, i);
        const char *text = lua_tostring(L, i);
        if (!size || size > 256 || std::any_of(text, text + size, [](unsigned char c) { return c < 32 || c == 127; })) {
            return Failure(L, "action labels require 1-256 UTF-8 bytes without control characters");
        }
        // JSON serialization validates UTF-8 before labels reach the shared renderer.
        try {
            (void) Json(std::string(text, size)).dump();
        } catch (const std::exception &) {
            return Failure(L, "action labels must be UTF-8");
        }
    }
    if (m.actions.size() >= 32 || m.actions.contains(id)) {
        return Failure(L, "duplicate action or at most 32 actions");
    }
    lua_pushvalue(L, 4);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    m.actions.emplace(id, Action{title, description, {}, ref});
    lua_pushboolean(L, true);
    return 1;
}

int Subscribe(lua_State *L) {
    auto       &m = Self(L);
    const char *event = luaL_checkstring(L, 1);
    if (!m.initializing || lua_rawlen(L, 1) != std::strlen(event) || !lua_isfunction(L, 2)) {
        return Failure(L, "subscribe requires a function during package initialization");
    }
    if (std::strcmp(event, "enemy_killed") && std::strcmp(event, "tick") && std::strcmp(event, "map_enter") && std::strcmp(event, "floor_enter") && std::strcmp(event, "game_enter") && std::strcmp(event, "game_leave") && std::strcmp(event, "save_loaded")) {
        return Failure(L, "unsupported lifecycle event");
    }
    if (m.listener_count >= 32) {
        return Failure(L, "at most 32 owned event listeners");
    }
    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    m.listeners[std::string("on_") + event].push_back(ref);
    ++m.listener_count;
    lua_pushboolean(L, true);
    return 1;
}

int SubscribeCustom(lua_State *L) {
    auto       &m = Self(L);
    const char *provider = luaL_checkstring(L, 1), *name = luaL_checkstring(L, 2);
    if (!m.initializing || lua_gettop(L) != 3 || !lua_isfunction(L, 3) || !Key(name) ||
        lua_rawlen(L, 1) != std::strlen(provider) || lua_rawlen(L, 2) != std::strlen(name)) {
        return Failure(L, "subscribe_custom requires provider, event name and function during initialization");
    }
    if (m.package.id != provider && std::ranges::find(m.package.dependencies, provider) == m.package.dependencies.end()) {
        return Failure(L, "custom event provider must be this package or a declared dependency");
    }
    if (m.listener_count >= 32) {
        return Failure(L, "at most 32 owned event listeners");
    }
    lua_pushvalue(L, 3);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    m.listeners[std::string("custom:") + provider + ":" + name].push_back(ref);
    ++m.listener_count;
    lua_pushboolean(L, true);
    return 1;
}

int ExportQuery(lua_State *L) {
    auto       &m = Self(L);
    const char *name = luaL_checkstring(L, 1);
    if (!m.initializing || !Key(name) || lua_rawlen(L, 1) != std::strlen(name) || !lua_isfunction(L, 2)) {
        return Failure(L, "export_query requires a named function during initialization");
    }
    if (m.exports.size() >= 32 || m.exports.contains(name)) {
        return Failure(L, "duplicate export or at most 32 query exports");
    }
    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    m.exports.emplace(name, ref);
    lua_pushboolean(L, true);
    return 1;
}

int InvokeQuery(lua_State *L) {
    auto       &m = Self(L);
    const char *provider = luaL_checkstring(L, 1), *name = luaL_checkstring(L, 2);
    if (!m.playing || !m.invoke) {
        return Failure(L, "query invocation requires gameplay");
    }
    if (lua_rawlen(L, 1) != std::strlen(provider) || lua_rawlen(L, 2) != std::strlen(name) || !Key(name)) {
        return Failure(L, "invalid query identity");
    }
    if (std::ranges::find(m.package.dependencies, provider) == m.package.dependencies.end()) {
        return Failure(L, "query provider must be a declared dependency");
    }
    if (m.service_depth >= 8 || --*m.service_budget < 0) {
        return Failure(L, "query invocation depth or call limit exceeded");
    }
    if (!ReadArguments(L, 3, m.arguments, m.call_error)) {
        return Failure(L, m.call_error.c_str());
    }
    m.call_error.clear();
    try {
        if (!m.invoke(m, provider, name, m.arguments, m.snapshot, m.call_error) && m.call_error.empty()) {
            m.call_error = "query export failed";
        }
    } catch (const std::exception &e) {
        m.call_error = e.what();
    }
    if (!m.call_error.empty()) {
        return Failure(L, m.call_error.c_str());
    }
    PushJson(L, m.snapshot);
    return 1;
}

int DispatchQuery(lua_State *L) {
    auto &m = Self(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, m.service_reference);
    PushJson(L, m.service_arguments);
    lua_call(L, 1, 1);
    if (!lua_istable(L, -1)) {
        return luaL_error(L, "query export must return an object");
    }
    if (!ReadArguments(L, -1, m.service_result, m.call_error)) {
        return luaL_error(L, "%s", m.call_error.c_str());
    }
    return 0;
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
        {"action",           RegisterAction },
        {"subscribe",        Subscribe      },
        {"subscribe_custom", SubscribeCustom},
        {"export_query",     ExportQuery    },
        {"invoke_query",     InvokeQuery    },
        {"get",              Get            },
        {"set",              Set            },
        {"quest_add",        QuestAdd       },
        {"quest_get",        QuestGet       },
        {"add_money",        AddMoney       },
        {"supports",         HasCapability  },
        {"player",           Player         },
        {"party",            Party          },
        {"inventory",        Inventory      },
        {"log",              Log            },
        {"resource",         Resource       },
        {"call",             ApiCall        },
        {"api_available",    ApiAvailable   },
        {"api_list",         ApiList        },
        {nullptr,            nullptr        }
    };
    luaL_setfuncs(L, api, 0);
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "api_version");
    lua_setglobal(L, "chronicle");
    lua_newtable(L);
    for (const auto &[id, data] : Self(L).resources) {
        lua_pushlstring(L, data.data(), data.size());
        lua_setfield(L, -2, id.c_str());
    }
    lua_setfield(L, LUA_REGISTRYINDEX, "chronicle.resources");
    return 0;
}

void PushWorld(lua_State *L, const World &world) {
    lua_newtable(L);
    lua_pushinteger(L, world.mode);
    lua_setfield(L, -2, "mode");
    lua_pushinteger(L, world.map);
    lua_setfield(L, -2, "map");
    lua_pushinteger(L, world.money);
    lua_setfield(L, -2, "money");
    lua_pushboolean(L, world.playing);
    lua_setfield(L, -2, "playing");
    lua_pushinteger(L, world.dungeon);
    lua_setfield(L, -2, "dungeon");
    lua_pushinteger(L, world.floor);
    lua_setfield(L, -2, "floor");
    if (!world.event.is_null()) {
        PushJson(L, world.event);
        lua_setfield(L, -2, "event");
    }
}

int Dispatch(lua_State *L) {
    auto &m = Self(L);
    if (m.action_reference != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, m.action_reference);
        PushWorld(L, *m.world);
        lua_call(L, 1, 0);
        return 0;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, m.reference);
    lua_getfield(L, -1, m.callback);
    if (!lua_isnil(L, -1)) {
        if (!lua_isfunction(L, -1)) {
            return luaL_error(L, "callback must be a function");
        }
        PushWorld(L, *m.world);
        lua_call(L, 1, 0);
    } else {
        lua_pop(L, 1);
    }
    if (const auto found = m.listeners.find(m.callback); found != m.listeners.end()) {
        for (const int ref : found->second) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            PushWorld(L, *m.world);
            lua_call(L, 1, 0);
        }
    }
    return 0;
}

int Reference(lua_State *L) {
    if (!lua_istable(L, 1)) {
        return luaL_error(L, "script must return a callback table");
    }
    lua_getfield(L, 1, "on_enemy_killed");
    if (!lua_isnil(L, -1) && !lua_isfunction(L, -1)) {
        return luaL_error(L, "on_enemy_killed must be a function");
    }
    if (lua_isfunction(L, -1)) {
        // An empty listener entry also marks a legacy returned callback as an
        // explicit subscription, without invoking it twice in Dispatch.
        Self(L).listeners.try_emplace("on_enemy_killed");
    }
    lua_pop(L, 1);
    Self(L).reference = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}
} // namespace

struct Runtime::Impl {
    std::vector<std::unique_ptr<Module>>             modules;
    Json                                             state = Json::object();
    std::vector<std::string>                         diagnostics;
    std::vector<std::string>                         logs;
    World                                            previous;
    bool                                             observed = false;
    bool                                             was_playing = false, loaded_pending = false;
    Manager                                          plan;
    std::vector<std::pair<std::string, std::string>> pending_actions;
    std::vector<CustomEvent>                         pending_events;

    // Prepare all allocations before publishing native writes. Swap after commit
    // cannot fail, so failed native/managed transactions never leak notifications.
    std::optional<std::vector<CustomEvent>> PrepareEvents(const Module &m, const World &world) {
        if (m.emissions.empty()) {
            return std::nullopt;
        }
        if (pending_events.size() + m.emissions.size() > 64) {
            throw std::runtime_error("custom event queue capacity (64) exceeded");
        }
        auto        next = pending_events;
        const World context{world.mode, world.map, 0, world.playing, world.dungeon, world.floor};
        for (const auto &args : m.emissions) {
            next.push_back({m.package.id, args, context});
        }
        return next;
    }

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

    bool Invoke(Module &caller, const char *provider, const char *name, const Json &args, Json &result, std::string &error) {
        for (auto &ptr : modules) {
            auto &m = *ptr;
            if (m.package.id != provider) {
                continue;
            }
            auto found = m.exports.find(name);
            if (m.failed || !m.lua || found == m.exports.end()) {
                error = "query provider or export is unavailable";
                return false;
            }
            // Provider callbacks have completed earlier in dependency order. No reentrant
            // provider may be entered; requests only flow toward declared dependencies.
            if (m.world) {
                error = "query provider is already executing";
                return false;
            }

            struct Restore {
                Module &m;

                ~Restore() {
                    m.world = nullptr;
                    m.read_game = nullptr;
                    m.query_game = nullptr;
                    m.commit_game = nullptr;
                    m.read_only = false;
                    m.playing = false;
                    m.service_depth = 0;
                    m.service_budget = &m.services_left;
                    lua_settop(m.lua, 0);
                }
            } restore{m};

            m.world = caller.world;
            m.read_game = caller.read_game;
            m.query_game = caller.query_game;
            m.playing = caller.playing;
            m.read_only = true;
            m.service_depth = caller.service_depth + 1;
            m.service_budget = caller.service_budget;
            m.service_reference = found->second;
            m.service_arguments = args;
            m.call_error.clear();
            lua_pushcfunction(m.lua, DispatchQuery);
            ResetBudget(m);
            const int status = lua_pcall(m.lua, 0, 0, 0);
            if (status != LUA_OK) {
                const char *text = lua_type(m.lua, -1) == LUA_TSTRING ? lua_tostring(m.lua, -1) : "non-text export error";
                error = std::string(text ? text : "query export failed").substr(0, 1024);
                return false;
            }
            result = m.service_result;
            return true;
        }
        error = "query provider is unavailable";
        return false;
    }

    void Call(Module &m, const char *name, const World &world, const std::function<bool(int)> &money, const ReadGame &read_game,
              const QueryGame &query_game, const CommitGame &commit_game, int action_reference = LUA_NOREF) {
        if (m.failed || !m.lua) {
            return;
        }
        auto      *L = m.lua;
        const Json before = *m.state;
        m.pending_money = 0;
        m.commands.clear();
        m.services_left = 16;
        m.emissions.clear();
        m.service_budget = &m.services_left;
        m.playing = world.playing;
        m.world = &world;
        m.read_game = &read_game;
        m.query_game = &query_game;
        m.commit_game = &commit_game;
        m.callback = name;
        m.action_reference = action_reference;
        lua_pushcfunction(L, Dispatch);
        ResetBudget(m);
        if (!Check(m, lua_pcall(L, 0, 0, 0))) {
            *m.state = before;
            m.commands.clear();
            m.emissions.clear();
            m.playing = false;
            m.world = nullptr;
            m.read_game = nullptr;
            m.query_game = nullptr;
            m.commit_game = nullptr;
            m.native.reset();
            return;
        }
        std::string error;
        const bool  writes = m.pending_money || !m.commands.empty();
        bool        accepted = !writes;
        try {
            auto events = PrepareEvents(m, world);
            if (writes) {
                accepted = commit_game ? commit_game(m.commands, m.pending_money, error)
                                       : m.commands.empty() && money && money(m.pending_money);
            }
            if (accepted && events) {
                pending_events.swap(*events);
            }
        } catch (const std::exception &e) {
            accepted = false;
            error = e.what();
        }
        if (!accepted) {
            *m.state = before;
            m.failed = true;
            diagnostics.push_back(m.package.id + ": game operation was rejected; script stopped: " + error);
        }
        m.pending_money = 0;
        m.commands.clear();
        m.emissions.clear();
        m.playing = false;
        m.world = nullptr;
        m.read_game = nullptr;
        m.query_game = nullptr;
        m.commit_game = nullptr;
        if (m.failed) {
            m.native.reset();
        }
    }

    void NativeCallback(Module &m, const World &world, const QueryGame &query, const CommitGame &commit, uint32_t event = 0, const char *action = nullptr) {
        if (m.failed || !m.native || (event && !m.native->WantsEvent(event))) {
            return;
        }
        const Json before = *m.state;
        m.native_active = true;
        m.playing = world.playing;
        m.query_game = &query;
        m.commit_game = &commit;
        m.commands.clear();
        m.native_calls = 0;
        m.emissions.clear();
        std::string error;
        bool        accepted = false;
        try {
            if (event || action) {
                auto payload_object = Json{
                    {"mode",    world.mode   },
                    {"map",     world.map    },
                    {"money",   world.money  },
                    {"playing", world.playing},
                    {"dungeon", world.dungeon},
                    {"floor",   world.floor  }
                };
                if (!world.event.is_null()) {
                    payload_object["event"] = world.event;
                }
                const auto payload = payload_object.dump();
                accepted = action ? m.native->RunAction(action, payload.c_str()) : m.native->Event(event, payload.c_str());
            } else {
                accepted = m.native->Tick();
            }
            if (!accepted) {
                error = "callback returned failure";
            }
            if (accepted) {
                auto events = PrepareEvents(m, world);
                if (!m.commands.empty()) {
                    accepted = commit && commit(m.commands, 0, error);
                }
                if (accepted && events) {
                    pending_events.swap(*events);
                }
            }
        } catch (const std::exception &e) {
            accepted = false;
            error = e.what();
        }
        m.playing = false;
        m.native_active = false;
        m.query_game = nullptr;
        m.commit_game = nullptr;
        m.commands.clear();
        m.emissions.clear();
        if (!accepted) {
            *m.state = before;
            m.failed = true;
            diagnostics.push_back(m.package.id + ": native callback stopped: " + error);
            m.native.reset();
        }
    }
};

Runtime::Runtime() : impl_(std::make_unique<Impl>()) {}

Runtime::~Runtime() = default;

void Runtime::Start(const Manager &manager) {
    ++epoch_;
    impl_->plan = manager;
    impl_->modules.clear();
    impl_->diagnostics.clear();
    impl_->logs.clear();
    impl_->observed = false;
    impl_->was_playing = false;
    impl_->loaded_pending = false;
    impl_->pending_actions.clear();
    impl_->pending_events.clear();
    for (const auto &id : manager.LoadOrder()) {
        const auto found = std::ranges::find(manager.Packages(), id, &Package::id);
        if (found == manager.Packages().end()) {
            continue;
        }
        if (found->script.empty() && found->quests.empty() && found->native_library.empty()) {
            continue;
        }
        auto  module = std::make_unique<Module>();
        auto &m = *module;
        m.package = *found;
        m.invoke = [host = impl_.get()](Module &caller, const char *provider, const char *name, const Json &args, Json &result, std::string &error) { return host->Invoke(caller, provider, name, args, result, error); };
        m.logs = &impl_->logs;
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
        if (!m.failed && Permitted(m, "resources.read") && !m.package.script.empty()) {
            try {
                for (const auto &[id, declared] : m.package.resources) {
                    const auto path = std::filesystem::canonical(declared);
                    const auto relative = path.lexically_relative(m.package.root);
                    if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." || !std::filesystem::is_regular_file(path)) {
                        throw std::runtime_error("resource no longer contained in package");
                    }
                    std::ifstream input(path, std::ios::binary);
                    std::string   data(65537, '\0');
                    input.read(data.data(), data.size());
                    if (!input.eof() || input.gcount() > 65536) {
                        throw std::runtime_error("resource unreadable or exceeds 64 KiB");
                    }
                    data.resize(static_cast<std::size_t>(input.gcount()));
                    m.resources.emplace(id, std::move(data));
                }
            } catch (const std::exception &error) {
                m.failed = true;
                impl_->diagnostics.push_back(id + ": " + error.what());
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
        m.initializing = false;
        if (!m.failed && !m.package.native_library.empty()) {
            m.native = std::make_unique<NativeLibrary>();
            std::string        error;
            ChronicleModHostV1 host{sizeof(ChronicleModHostV1), 1, &m, NativeAvailable, NativeCall, NativeLog};
            ChronicleModHostV2 host2{sizeof(ChronicleModHostV2), 2, &m, NativeAvailable, NativeCall, NativeLog, NativeStateGet, NativeStateSet};
            if (!m.native->Start(m.package, host, host2, error)) {
                m.failed = true;
                impl_->diagnostics.push_back(id + ": " + error);
                m.native.reset();
            } else {
                try {
                    auto actions = m.actions;
                    for (const auto &action : m.native->Actions()) {
                        if (!Key(action.id) || actions.contains(action.id) || actions.size() >= 32) {
                            throw std::runtime_error("invalid/duplicate native action ID or more than 32 combined actions");
                        }
                        for (const char *label : {action.title, action.description}) {
                            const auto size = std::strlen(label);
                            if (!size || size > 256 || std::any_of(label, label + size, [](unsigned char c) { return c < 32 || c == 127; })) {
                                throw std::runtime_error("native action labels require 1-256 UTF-8 bytes without controls");
                            }
                            (void) Json(std::string(label, size)).dump();
                        }
                        actions.emplace(action.id, Action{action.title, action.description, {}, LUA_NOREF, true});
                    }
                    m.actions = std::move(actions);
                } catch (const std::exception &e) {
                    m.failed = true;
                    impl_->diagnostics.push_back(id + ": " + e.what());
                    m.native.reset();
                }
            }
        }
        impl_->modules.push_back(std::move(module));
    }
}

void Runtime::Tick(const World &world, const std::function<bool(int)> &money, const ReadGame &read_game,
                   const QueryGame &query_game, const CommitGame &commit_game,
                   std::span<const EnemyDeath> deaths, unsigned dropped_deaths) {
    if (dropped_deaths) {
        impl_->diagnostics.push_back("Enemy event queue overflow: " + std::to_string(dropped_deaths) + " deaths dropped.");
    }
    // UI requests are consumed exactly once, never carried into another session.
    const auto actions = std::exchange(impl_->pending_actions, {});
    const auto events = std::exchange(impl_->pending_events, {});
    const bool action_context = world.playing && impl_->was_playing && world.mode == impl_->previous.mode &&
                                world.map == impl_->previous.map && world.dungeon == impl_->previous.dungeon && world.floor == impl_->previous.floor;
    for (auto &ptr : impl_->modules) {
        for (const auto &[owner, id] : actions) {
            if (owner == ptr->package.id) {
                ptr->actions.at(id).result = "Canceled: gameplay context changed or package unavailable.";
            }
        }
    }
    const bool game_enter = world.playing && !impl_->was_playing;
    const bool game_leave = !world.playing && impl_->was_playing;
    const bool floor_enter = world.playing && world.dungeon >= 0 && world.floor >= 0 && (!impl_->observed || world.dungeon != impl_->previous.dungeon || world.floor != impl_->previous.floor);
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
            m.native.reset();
            continue;
        }
        if (world.playing && impl_->loaded_pending) {
            impl_->Call(m, "on_save_loaded", world, money, read_game, query_game, commit_game);
            impl_->NativeCallback(m, world, query_game, commit_game, CHRONICLE_EVENT_SAVE_LOADED);
        }
        if (game_enter) {
            impl_->Call(m, "on_game_enter", world, money, read_game, query_game, commit_game);
            impl_->NativeCallback(m, world, query_game, commit_game, CHRONICLE_EVENT_GAME_ENTER);
        }
        if (game_leave) {
            impl_->Call(m, "on_game_leave", world, money, read_game, query_game, commit_game);
            impl_->NativeCallback(m, world, query_game, commit_game, CHRONICLE_EVENT_GAME_LEAVE);
        }
        if (entered && !m.failed) {
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
            impl_->Call(m, "on_map_enter", world, money, read_game, query_game, commit_game);
            impl_->NativeCallback(m, world, query_game, commit_game, CHRONICLE_EVENT_MAP_ENTER);
        }
        if (floor_enter) {
            impl_->Call(m, "on_floor_enter", world, money, read_game, query_game, commit_game);
            impl_->NativeCallback(m, world, query_game, commit_game, CHRONICLE_EVENT_FLOOR_ENTER);
        }
        if (world.playing) {
            for (const auto &event : events) {
                if (m.failed || impl_->loaded_pending || !action_context || event.context.mode != world.mode ||
                    event.context.map != world.map || event.context.dungeon != world.dungeon || event.context.floor != world.floor) {
                    continue;
                }
                const auto source = std::ranges::find_if(impl_->modules, [&](const auto &p) { return p->package.id == event.owner; });
                if (source == impl_->modules.end() || (*source)->failed) {
                    continue;
                }
                if (event.owner != m.package.id && std::ranges::find(m.package.dependencies, event.owner) == m.package.dependencies.end()) {
                    continue;
                }
                World event_world = world;
                event_world.event = {
                    {"owner",   event.owner                  },
                    {"name",    event.arguments.at("name")   },
                    {"payload", event.arguments.at("payload")}
                };
                const auto key = "custom:" + event.owner + ":" + event.arguments.at("name").get<std::string>();
                if (m.listeners.contains(key)) {
                    impl_->Call(m, key.c_str(), event_world, money, read_game, query_game, commit_game);
                }
                impl_->NativeCallback(m, event_world, query_game, commit_game, CHRONICLE_EVENT_CUSTOM);
            }
            if (!game_enter && !floor_enter && !impl_->loaded_pending) {
                for (const auto &death : deaths.first(std::min<std::size_t>(deaths.size(), 64))) {
                    if (death.dungeon != world.dungeon || death.floor != world.floor) {
                        continue;
                    }
                    World event_world = world;
                    event_world.event = {
                        {"entity",       std::to_string(death.entity)          },
                        {"attacker",     death.attacker                        },
                        {"reward_exp",   death.experience                      },
                        {"reward_money", death.money                           },
                        {"damage",       death.damage                          },
                        {"source",       death.scripted ? "scripted" : "native"}
                    };
                    if (m.listeners.contains("on_enemy_killed")) {
                        impl_->Call(m, "on_enemy_killed", event_world, money, read_game, query_game, commit_game);
                    }
                    impl_->NativeCallback(m, event_world, query_game, commit_game, CHRONICLE_EVENT_ENEMY_KILLED);
                }
            }
            for (const auto &[owner, id] : actions) {
                if (owner == m.package.id && action_context && !m.failed) {
                    auto &action = m.actions.at(id);
                    if (action.native) {
                        impl_->NativeCallback(m, world, query_game, commit_game, 0, id.c_str());
                    } else {
                        impl_->Call(m, "menu action", world, money, read_game, query_game, commit_game, action.reference);
                    }
                    action.result = m.failed ? "Failed. See module diagnostics." : "Completed.";
                }
            }
            impl_->Call(m, "on_tick", world, money, read_game, query_game, commit_game);
            impl_->NativeCallback(m, world, query_game, commit_game);
        }
    }
    impl_->was_playing = world.playing;
    if (world.playing) {
        impl_->loaded_pending = false;
        impl_->previous = world;
        impl_->observed = true;
    }
}

bool Runtime::HasGameplay() const {
    return std::ranges::any_of(impl_->modules, [](const auto &m) { return !m->failed; });
}

bool Runtime::WantsEnemyDeaths() const {
    return std::ranges::any_of(impl_->modules, [](const auto &m) {
        return !m->failed && (m->listeners.contains("on_enemy_killed") ||
                              (m->native && m->native->WantsEvent(CHRONICLE_EVENT_ENEMY_KILLED)));
    });
}

std::vector<std::string> Runtime::ActiveOwners() const {
    std::vector<std::string> owners;
    for (const auto &module : impl_->modules) {
        if (!module->failed) {
            owners.push_back(module->package.id);
        }
    }
    return owners;
}

std::vector<ActionStatus> Runtime::Actions(std::string_view owner) const {
    std::vector<ActionStatus> result;
    for (const auto &m : impl_->modules) {
        if (m->package.id == owner) {
            for (const auto &[id, action] : m->actions) {
                result.push_back({m->package.id, id, action.title, action.description, action.result,
                                  !m->failed && impl_->was_playing});
            }
        }
    }
    return result;
}

bool Runtime::QueueAction(std::string_view owner, std::string_view id, std::string &error) {
    error.clear();
    const std::pair<std::string, std::string> request{owner, id};
    if (!impl_->was_playing) {
        error = "Actions require gameplay.";
    } else if (impl_->pending_actions.size() >= 16) {
        error = "At most 16 queued actions.";
    } else if (std::ranges::find(impl_->pending_actions, request) != impl_->pending_actions.end()) {
        error = "Action is already queued.";
    } else {
        for (auto &m : impl_->modules) {
            if (m->package.id != owner || m->failed) {
                continue;
            }
            const auto found = m->actions.find(std::string(id));
            if (found != m->actions.end()) {
                impl_->pending_actions.push_back(request);
                found->second.result = "Queued for gameplay.";
                return true;
            }
        }
        error = "Action or package is unavailable.";
    }
    return false;
}

std::vector<std::string> Runtime::Logs() const { return impl_->logs; }

void Runtime::ResetState() {
    impl_->modules.clear();
    impl_->state = Json::object();
    Start(impl_->plan);
    impl_->loaded_pending = true;
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
        staged.impl_->loaded_pending = true;
        impl_.swap(staged.impl_);
        ++epoch_;
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
