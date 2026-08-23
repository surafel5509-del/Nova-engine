-- Sample Platformer player controller (used by the :game export test).
local speed = 5.0
local jumpVelocity = 9.0

function on_update(id, dt)
    local ax, ay = nova.input_axis()
    local vx, vy = nova.get_velocity(id)
    nova.set_velocity(id, ax * speed, vy)

    if nova.input_jump() and nova.is_grounded(id) then
        nova.set_velocity(id, ax * speed, jumpVelocity)
    end
end
