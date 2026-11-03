-- Prevent the player from being stomped on.
players[1]:set_pre_stomped_by(function(_, _)
    return true
end)
