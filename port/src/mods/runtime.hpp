#pragma once
#include <functional>
#include <memory>
#include <string>

#include "manager.hpp"

namespace mods {
struct World {
    int  mode = -1, map = -1, money = 0;
    bool playing = false;
};

struct QuestStatus {
    std::string owner, id, title, description;
    int         progress = 0, goal = 1;
};

class Runtime {
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime                 &operator=(const Runtime &) = delete;
    void                     Start(const Manager &manager);
    void                     Tick(const World &world, const std::function<bool(int)> &add_money = {});
    void                     ResetState();
    std::string              SaveState() const;
    bool                     LoadState(const std::string &json, std::string &error);
    std::vector<QuestStatus> Quests() const;
    std::vector<std::string> Diagnostics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Runtime &Scripts();
void     TickGame();
} // namespace mods
