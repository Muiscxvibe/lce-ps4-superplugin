-- retailall: script-staged content + events (runs during REGISTER on the console)
lce.register_item{key = "retailall:script_gem", name = "Script Gem", texture = "retailall:script_gem"}
lce.subscribe_tick(2, function(e)
    local stats = lce.tick_stats()
    lce.log("paced tick " .. e.tick .. " after " .. stats.callbacks_skipped .. " skips")
end)
lce.subscribe("PlayerJoin", function(e) lce.log("joined: " .. tostring(e.name)) end)
lce.subscribe("Chat", function(e) if e.message == "!cancel" then return true end end)
