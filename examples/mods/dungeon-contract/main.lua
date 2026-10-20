-- Actions are registered once at script startup; quest state is save-owned.
local function register_contract()
    return assert(chronicle.call("quests.register", {
        id = "contract", title = "Dungeon Contract",
        description = "Start this contract, enter a dungeon floor and defeat two enemies.",
        explicit_start = true,
        objectives = {
            {id = "explore", title = "Enter a dungeon floor", goal = 1,
             event = "floor_enter"},
            {id = "hunt", title = "Defeat two enemies", goal = 2,
             event = "enemy_killed", match = {source = "native"}}
        },
        rewards = {{operation = "economy.add", arguments = {amount = 50}}}
    }))
end

assert(chronicle.action("start", "Start contract",
    "Begin the contract. Completed contracts cannot be repeated.", function()
        local quest = register_contract()
        if quest.status == "registered" then
            assert(chronicle.call("quests.start", {id = "contract"}))
        end
    end))

assert(chronicle.action("stop", "Abandon contract",
    "Discard unfinished progress. Start again later; completed contracts stay closed.", function()
        local quest = register_contract()
        if quest.status == "active" or quest.status == "failed" then
            assert(chronicle.call("quests.abandon", {id = "contract"}))
        end
    end))

return {on_game_enter = function() register_contract() end}
