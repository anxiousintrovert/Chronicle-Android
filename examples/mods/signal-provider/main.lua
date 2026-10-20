-- Demonstration only: a shared-menu action stands in for a content extension.
assert(chronicle.action("signal", "Send bridge signal",
    "Complete the example signal quest on the next gameplay tick.", function()
        assert(chronicle.call("events.emit", {
            name = "challenge.complete", payload = {arena = "bridge", success = true}
        }))
    end))
return {}
