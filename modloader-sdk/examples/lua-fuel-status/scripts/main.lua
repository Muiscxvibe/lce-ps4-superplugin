local paper = "minecraft:paper"

lce.subscribe("WorldLoad", function()
    local ticks = lce.fuel_ticks(paper)
    if ticks == nil then
        lce.log("No SDK fuel duration is available for " .. paper)
        return
    end
    lce.log(("%s burns for %d ticks; stock Coal entry = %s")
        :format(paper, ticks, tostring(lce.fuel_ticks("minecraft:coal"))))
end)
