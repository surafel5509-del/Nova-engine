-- Asteroids drift down; when they pass the ship, bump the shared dodge score.
local fallSpeed = 2.5

function on_update(id, dt)
    local x, y = nova.get_position(id)
    y = y - fallSpeed * dt
    if y < -6 then
        y = 7 + math.random(0, 3)
        x = math.random(-3, 3)
        _G.dodged = (_G.dodged or 0) + 1
        nova.set_ui_text("ui-score", "Dodged: " .. _G.dodged)
    end
    nova.set_position(id, x, y)
end
