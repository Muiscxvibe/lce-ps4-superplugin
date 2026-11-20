-- Example LCE mod script (ships as scripts/main.lua, section LCE_SEC_SCRIPT=14).
-- Declarative registration is better done in mod.json; scripts add behaviour.
registerItem("rubymod:ruby", "Ruby", "res/ruby.png")
registerShapedRecipe("rubymod:block", {"RRR","RRR","RRR"}, {R="rubymod:ruby"}, "rubymod:ruby_block", {count=1})

local hits = 0
lce.subscribe("ServerTick", function(e)
  hits = hits + 1
  if hits % 1200 == 0 then lce.send_chat("tick "..e.tick) end
end)

lce.subscribe("PlayerJoin", function(e)
  lce.send_chat("Welcome, "..(e.name or "player").."!")
end)

-- Cancel chat lines that contain a banned word.
lce.subscribe("Chat", function(e)
  if e.message and e.message:find("spam") then return true end
  return false
end)
