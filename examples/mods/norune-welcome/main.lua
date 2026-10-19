-- All persistent values belong to this mod and travel with the selected save.
return {
    on_tick = function(world)
        if chronicle.quest_get("welcome") >= 1 and not chronicle.get("reward_paid") then
            assert(chronicle.add_money(50))
            assert(chronicle.set("reward_paid", true))
        end
    end
}
