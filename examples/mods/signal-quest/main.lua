return {on_game_enter = function()
    assert(chronicle.call("quests.register", {
        id = "bridge", title = "Bridge Signal",
        description = "Use Send bridge signal in the shared mod actions page.",
        objectives = {{id = "signal", title = "Receive the bridge signal", goal = 1,
            event = "custom", owner = "example.signal-provider", name = "challenge.complete",
            payload_match = {arena = "bridge", success = true}}}
    }))
end}
