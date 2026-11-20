-- retailall: script-staged content + events (runs during REGISTER on the console)
lce.register_item{key = "retailall:script_gem", name = "Script Gem", texture = "retailall:script_gem"}
local ticks = 0
lce.subscribe("ServerTick", function(e) ticks = ticks + 1 end)
lce.subscribe("PlayerJoin", function(e) lce.log("joined: " .. tostring(e.name)) end)
lce.subscribe("Chat", function(e) if e.message == "!cancel" then return true end end)
