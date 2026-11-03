-- Ignore every stun on the player lasting 100 frames or more.
players[1]:set_pre_stun(function(_, framecount)
    return framecount >= 100
end)
