#pragma once
#include <functional>
#pragma push_macro("B")
#pragma push_macro("R")
#pragma push_macro("G")
#pragma push_macro("A")
#undef B
#undef R
#undef G
#undef A
#include <nlohmann/json.hpp>
#pragma pop_macro("A")
#pragma pop_macro("G")
#pragma pop_macro("R")
#pragma pop_macro("B")
#include <memory>
#include <string>

#include "manager.hpp"

namespace mods {
using ReadGame = std::function<nlohmann::json(std::string_view)>;

struct GameCommand {
    std::string    operation;
    nlohmann::json arguments;
};

using QueryGame = std::function<nlohmann::json(std::string_view, const nlohmann::json &)>;
using CommitGame = std::function<bool(const std::vector<GameCommand> &, int, std::string &)>;

struct World {
    int  mode = -1, map = -1, money = 0;
    bool playing = false;
    int  dungeon = -1, floor = -1;
};

struct QuestStatus {
    std::string owner, id, title, description;
    int         progress = 0, goal = 1;
};

struct ActionStatus {
    std::string owner, id, title, description, result;
    bool        available = false;
};

class Runtime {
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime                  &operator=(const Runtime &) = delete;
    void                      Start(const Manager &manager);
    void                      Tick(const World &world, const std::function<bool(int)> &add_money = {}, const ReadGame &read_game = {},
                                   const QueryGame &query_game = {}, const CommitGame &commit_game = {});
    void                      ResetState();
    bool                      HasGameplay() const;
    std::vector<std::string>  Logs() const;
    std::string               SaveState() const;
    bool                      LoadState(const std::string &json, std::string &error);
    std::vector<QuestStatus>  Quests() const;
    std::vector<ActionStatus> Actions(std::string_view owner) const;
    bool                      QueueAction(std::string_view owner, std::string_view id, std::string &error);
    std::vector<std::string>  Diagnostics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Runtime &Scripts();
void     TickGame();
} // namespace mods
