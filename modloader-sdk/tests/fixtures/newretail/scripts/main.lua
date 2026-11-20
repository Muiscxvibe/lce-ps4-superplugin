-- EFFECT_TICK for mod status effects (effects195 on_tick -> LCE_EV_EFFECT_TICK)
lce.subscribe("EffectTick", function(e)
  lce.log("effect tick " .. tostring(e.effect) .. " amp " .. tostring(e.amplifier) .. " id " .. tostring(e.effect_id))
end)
-- custom audio (audio195): right-click with a Glow Gem plays the chime at the player
lce.subscribe("ItemUse", function(e)
  if e.level and lce.resolve_item("newretail:gem") == e.item_id then
    lce.play_sound(e.level, e.x, e.y, e.z, "newretail:chime")
  end
end)
-- API 1.4 mod networking: a channel of this mod (client -> server payloads arrive as CustomPayload)
local rc = lce.register_channel("newretail:sync")
lce.log("register_channel newretail:sync rc " .. tostring(rc))
lce.log("register_channel foreign rc " .. tostring(lce.register_channel("othermod:x")))
lce.subscribe("CustomPayload", function(e) lce.log("payload " .. tostring(e.channel) .. " " .. tostring(e.len)) end)
