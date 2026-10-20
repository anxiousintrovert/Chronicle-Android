local function register_tree()
    assert(chronicle.call("skills.register", {
        id = "combat", title = "Combat Training", description = "Free manual training example.",
        nodes = {
            {id = "focus", title = "Focus", description = "Train explicitly for more outgoing damage.",
             max_rank = 2, effects = {enemy_damage_per_rank = 0.10}},
            {id = "guard", title = "Guard", description = "Reduce native dungeon life deductions.",
             max_rank = 2, prerequisites = {{id = "focus", rank = 1}},
             effects = {incoming_damage_reduction_per_rank = 0.05}}
        }
    }))
end

assert(chronicle.subscribe("skill_train_requested", function(world)
    local requested = world.event
    if requested.tree ~= "combat" then return end
    local node = assert(chronicle.call("skills.get", requested))
    if node.available then assert(chronicle.call("skills.unlock", requested)) end
end))

assert(chronicle.action("reset", "Reset example training",
    "Remove all example ranks and their effects. Training is free; there is no refund.", function()
        register_tree()
        -- Reset dependent nodes too; resetting a prerequisite alone retains their ranks.
        assert(chronicle.call("skills.set_rank", {tree = "combat", id = "guard", rank = 0}))
        assert(chronicle.call("skills.set_rank", {tree = "combat", id = "focus", rank = 0}))
    end))

return {on_game_enter = register_tree}
