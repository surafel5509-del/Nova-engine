-- Ball: launch on button, break bricks on contact, respawn on fall.
local launched = false
local brickIds = {"brick-1", "brick-2", "brick-3", "brick-4", "brick-5"}
local remaining = 5

function on_update(id, dt)
    local x, y = nova.get_position(id)

    if not launched then
        -- Keep ball parked above the paddle until launch.
        local px, py = nova.get_position("paddle-1")
        nova.set_position(id, px, py + 0.7)
        nova.set_velocity(id, 0, 0)
        if nova.ui_pressed("ui-launch") then
            launched = true
            nova.set_velocity(id, 3.5, 5.5)
            nova.log("ball launched")
        end
        return
    end

    -- Break bricks on proximity (physics bounces the ball off them).
    for _, b in ipairs(brickIds) do
        local bx, by = nova.get_position(b)
        if bx ~= nil and by > -100 then
            local dx = x - bx
            local dy = y - by
            if math.abs(dx) < 1.0 and math.abs(dy) < 0.55 then
                nova.set_position(b, bx, -200)  -- park broken brick offscreen
                remaining = remaining - 1
                nova.set_ui_text("ui-score", "Bricks: " .. remaining)
                nova.log("brick down: " .. remaining)
            end
        end
    end

    -- Respawn below the paddle.
    if y < -8 then
        local px, py = nova.get_position("paddle-1")
        nova.set_position(id, px, py + 0.7)
        nova.set_velocity(id, 3.5, 5.5)
        nova.log("ball respawned")
    end
end
