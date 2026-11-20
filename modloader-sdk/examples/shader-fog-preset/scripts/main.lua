if type(lce.shader_preset_info) ~= "function" then
    lce.log("Fog preset status is unavailable in this loader.")
    return
end

local fog = lce.shader_preset_info("fog_color_v1")
local message = ("fog_color_v1 status=%s available=%s active=%s"):format(
    fog.status, tostring(fog.available), tostring(fog.active))
if fog.valid then
    message = message .. (" rgb=#%02X%02X%02X weight=%d/%d"):format(
        fog.red, fog.green, fog.blue, fog.weight, fog.max_weight)
end
lce.log(message)
