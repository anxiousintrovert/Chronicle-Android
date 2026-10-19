#include "arguments.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
extern "C" {
#include "lua.h"
}

namespace mods {
namespace {
using Json = nlohmann::json;
constexpr unsigned max_depth = 8, max_nodes = 128, max_bytes = 16384;

struct Budget {
    unsigned                                nodes = 0, bytes = 0;
    std::array<const void *, max_depth + 1> ancestors{};
};

void Check(unsigned depth, Budget &budget, unsigned bytes = 0) {
    if (depth > max_depth || ++budget.nodes > max_nodes || bytes > max_bytes - budget.bytes) {
        throw std::runtime_error("host arguments exceed depth, node or byte limit");
    }
    budget.bytes += bytes;
}

void CheckJson(const Json &value, unsigned depth, Budget &budget) {
    Check(depth, budget);
    if (value.is_object()) {
        for (const auto &[key, child] : value.items()) {
            if (key.empty() || key.size() > 128 || key.find('\0') != std::string::npos) {
                throw std::runtime_error("host argument keys must be 1-128 bytes without NUL");
            }
            Check(depth, budget, static_cast<unsigned>(key.size()));
            CheckJson(child, depth + 1, budget);
        }
    } else if (value.is_array()) {
        for (const auto &child : value) {
            CheckJson(child, depth + 1, budget);
        }
    } else if (value.is_string()) {
        const auto &s = value.get_ref<const std::string &>();
        if (s.size() > max_bytes - budget.bytes) {
            throw std::runtime_error("host argument string too large");
        }
        budget.bytes += static_cast<unsigned>(s.size());
    } else if (value.is_number_float() && !std::isfinite(value.get<double>())) {
        throw std::runtime_error("host arguments must be finite");
    }
}

void Read(lua_State *L, int index, Json &out, unsigned depth, Budget &budget) {
    Check(depth, budget);
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
        case LUA_TNIL:
            out = nullptr;
            return;
        case LUA_TBOOLEAN:
            out = lua_toboolean(L, index) != 0;
            return;
        case LUA_TNUMBER:
            if (!std::isfinite(lua_tonumber(L, index))) {
                throw std::runtime_error("host arguments must be finite");
            }
            if (lua_isinteger(L, index)) {
                out = lua_tointeger(L, index);
            } else {
                out = lua_tonumber(L, index);
            }
            return;
        case LUA_TSTRING: {
            size_t      length;
            const char *s = lua_tolstring(L, index, &length);
            if (length > max_bytes - budget.bytes) {
                throw std::runtime_error("host argument string too large");
            }
            budget.bytes += static_cast<unsigned>(length);
            out = std::string(s, length);
            return;
        }
        case LUA_TTABLE:
            break;
        default:
            throw std::runtime_error("host arguments support only JSON values");
    }
    // lua_next is raw: no user metamethod can run while C++ values are live.
    // Reserve stack before pushing; never call luaL_error from this converter.
    if (!lua_checkstack(L, 3)) {
        throw std::runtime_error("host argument stack exhausted");
    }
    const void *identity = lua_topointer(L, index);
    for (unsigned i = 0; i < depth; ++i) {
        if (budget.ancestors[i] == identity) {
            throw std::runtime_error("cyclic host arguments");
        }
    }
    budget.ancestors[depth] = identity;
    out = Json::object();
    unsigned count = 0, largest = 0;
    bool     array = false, object = false;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        ++count;
        if (lua_type(L, -2) == LUA_TSTRING) {
            if (array) {
                throw std::runtime_error("mixed array and object argument keys");
            }
            object = true;
            size_t      length;
            const char *key = lua_tolstring(L, -2, &length);
            if (!length || length > 128 || std::memchr(key, 0, length)) {
                throw std::runtime_error("invalid host argument key");
            }
            Check(depth, budget, static_cast<unsigned>(length));
            Read(L, -1, out[std::string(key, length)], depth + 1, budget);
        } else if (lua_isinteger(L, -2)) {
            const auto key = lua_tointeger(L, -2);
            if (object || key < 1 || key > max_nodes) {
                throw std::runtime_error("array keys must be consecutive positive integers");
            }
            if (!array) {
                out = Json::array();
            }
            array = true;
            largest = std::max(largest, static_cast<unsigned>(key));
            Read(L, -1, out[static_cast<size_t>(key - 1)], depth + 1, budget);
        } else {
            throw std::runtime_error("invalid host argument key type");
        }
        lua_pop(L, 1);
    }
    if (array && largest != count) {
        throw std::runtime_error("sparse host argument array");
    }
    budget.ancestors[depth] = nullptr;
}
} // namespace

std::string ValidateArguments(const nlohmann::json &value) {
    try {
        if (!value.is_object()) {
            return "host arguments must be an object";
        }
        Budget budget;
        CheckJson(value, 0, budget);
        if (value.dump().size() > max_bytes) {
            return "encoded host arguments exceed 16 KiB";
        }
    } catch (const std::exception &e) {
        return e.what();
    }
    return {};
}

bool ReadArguments(lua_State *L, int index, nlohmann::json &value, std::string &error) {
    const int top = lua_gettop(L);
    try {
        Budget budget;
        if (lua_isnoneornil(L, index)) {
            value = Json::object();
        } else {
            Read(L, index, value, 0, budget);
        }
        error = ValidateArguments(value);
    } catch (const std::exception &e) {
        error = e.what();
    }
    lua_settop(L, top);
    if (!error.empty()) {
        value = Json::object();
    }
    return error.empty();
}
} // namespace mods
