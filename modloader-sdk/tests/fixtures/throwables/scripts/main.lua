-- throwables: report every hit of a mod projectile (server thread, after the declarative effects)
local hits = 0
lce.subscribe("ProjectileHit", function(e)
  hits = hits + 1
  if e.entity then
    lce.log("projectile " .. tostring(e.item_id) .. " hit an entity at " .. e.x .. "," .. e.y .. "," .. e.z .. " (hit " .. hits .. ")")
  else
    lce.log("projectile " .. tostring(e.item_id) .. " hit a block at " .. e.x .. "," .. e.y .. "," .. e.z .. " (hit " .. hits .. ")")
  end
end)
