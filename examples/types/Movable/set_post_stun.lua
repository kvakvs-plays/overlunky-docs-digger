-- Print how long the player was stunned each time they get stunned.
players[1]:set_post_stun(function(_, framecount)
    print("stunned for " .. framecount .. " frames")
end)
