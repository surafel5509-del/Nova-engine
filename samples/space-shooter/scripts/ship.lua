-- Ship: move with input, dodge asteroids, fire to destroy them (proximity zap).
local speed = 6.0

function on_update(id, dt)
    local ax, ay = nova.input_axis()
    local x, y = nova.get_position(id)
    nova.set_position(id, x + ax * speed * dt, math.max(-5.5, math.min(5.5, y + ay * speed * dt)))

    -- Fire: destroy any asteroid near the ship's column, respawn it at top.
    if nova.ui_pressed("ui-fire") then
        local px, py = nova.get_position(id)
        for _, rock in ipairs({"rock-1", "rock-2", "rock-3"}) do
            local rx, ry = nova.get_position(rock)
            if rx ~= nil and ry < py + 0.5 and math.abs(rx - px) < 1.2 then
                nova.set_position(rock, math.random(-3, 3), 8)
                nova.log("asteroid destroyed: " .. rock)
            end
        end
    end
end
