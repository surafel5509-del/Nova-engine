-- Platformer player: input movement + jump + coin pickup + UI.
local speed = 5.0
local jumpVelocity = 9.0
local score = 0

function on_start(id)
    nova.log("platformer ready")
end

function on_update(id, dt)
    local ax, ay = nova.input_axis()
    local vx, vy = nova.get_velocity(id)
    nova.set_velocity(id, ax * speed, vy)

    -- Jump from the on-screen button or the input jump flag.
    if (nova.input_jump() or nova.ui_pressed("ui-jump")) and nova.is_grounded(id) then
        nova.set_velocity(id, ax * speed, jumpVelocity)
    end

    -- Coin pickup by proximity.
    local px, py = nova.get_position(id)
    local cx, cy = nova.get_position("coin-1")
    if cx ~= nil then
        local dx = px - cx
        local dy = py - cy
        if (dx*dx + dy*dy) < 1.0 then
            score = score + 1
            nova.set_ui_text("ui-score", "Score: " .. score)
            nova.set_position("coin-1", cx + 6, cy)  -- move coin (endless pickup loop)
            nova.log("coin collected: " .. score)
        end
    end
end
