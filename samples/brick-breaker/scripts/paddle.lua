-- Paddle follows the horizontal input axis.
local speed = 8.0

function on_update(id, dt)
    local ax, ay = nova.input_axis()
    local x, y = nova.get_position(id)
    x = math.max(-3.2, math.min(3.2, x + ax * speed * dt))
    nova.set_position(id, x, y)
end
