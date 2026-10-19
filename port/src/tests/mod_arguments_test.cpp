#include <gtest/gtest.h>

#include "mods/arguments.hpp"
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace {
using Json = nlohmann::json;

class ModArguments : public testing::Test {
protected:
    lua_State *lua = luaL_newstate();

    void SetUp() override {
        ASSERT_NE(lua, nullptr);
        luaL_openlibs(lua);
    }

    void TearDown() override { lua_close(lua); }

    void Evaluate(const char *code) { ASSERT_EQ(luaL_dostring(lua, code), LUA_OK); }
};

TEST_F(ModArguments, StructuredValuesPreserveTypesWithoutMetamethodCalls) {
    Evaluate(R"(return {id="author.blade",enabled=true,steps={{at=1.5,text="hello"},{at=2,text="world"}},meta=setmetatable({}, {__pairs=function() error("must not run") end})})");
    Json        value;
    std::string error;
    ASSERT_TRUE(mods::ReadArguments(lua, -1, value, error)) << error;
    EXPECT_EQ(value["id"], "author.blade");
    EXPECT_EQ(value["enabled"], true);
    ASSERT_TRUE(value["steps"].is_array());
    EXPECT_EQ(value["steps"][0]["at"], 1.5);
    EXPECT_TRUE(value["steps"][1]["at"].is_number_integer());
    EXPECT_EQ(value["meta"], Json::object());
    EXPECT_EQ(lua_gettop(lua), 1);
}

TEST_F(ModArguments, RejectsUnsafeShapesAndRestoresStack) {
    for (const auto *code : {
             "local t={} t.self=t return t",
             "return {items={[1]=1,[3]=3}}",
             "return {items={[1]=1,name='mixed'}}",
             "return {callback=function() end}",
             "return {number=0/0}",
             "return {number=math.huge}",
             "return {[string.char(0)]='bad key'}",
             "return {text=string.rep('x',16385)}",
             "local t={} local a=t for i=1,9 do a.child={} a=a.child end return t",
             "local t={} for i=1,129 do t['key'..i]=i end return t"}) {
        lua_settop(lua, 0);
        Evaluate(code);
        Json        value;
        std::string error;
        EXPECT_FALSE(mods::ReadArguments(lua, -1, value, error)) << code;
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(lua_gettop(lua), 1);
        EXPECT_EQ(value, Json::object());
    }
}

TEST_F(ModArguments, FailedReadDoesNotPoisonFollowingCalls) {
    Evaluate("return {bad=function() end}, {id=31,value=-2147483648}");
    Json        value;
    std::string error;
    EXPECT_FALSE(mods::ReadArguments(lua, 1, value, error));
    ASSERT_TRUE(mods::ReadArguments(lua, 2, value, error)) << error;
    EXPECT_EQ(value["value"], -2147483648LL);
    EXPECT_EQ(lua_gettop(lua), 2);
}

TEST(ModArgumentJson, NativePayloadLimitsMatchLuaAndRejectInvalidUtf8) {
    EXPECT_TRUE(mods::ValidateArguments({
                                            {"id",    "author.blade"                },
                                            {"steps", Json::array({1, true, "text"})}
    })
                    .empty());
    EXPECT_FALSE(mods::ValidateArguments(Json::array()).empty());
    EXPECT_FALSE(mods::ValidateArguments({
                                             {"bad", std::string(1, static_cast<char>(0xff))}
    })
                     .empty());
    Json  root = Json::object();
    auto *node = &root;
    for (int i = 0; i < 9; ++i) {
        (*node)["child"] = Json::object();
        node = &(*node)["child"];
    }
    EXPECT_FALSE(mods::ValidateArguments(root).empty());
}
} // namespace
