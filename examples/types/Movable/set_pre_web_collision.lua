-- Let the player pass through webs without getting stuck.
players[1]:set_pre_web_collision(function(_, _)
    return true
end)
