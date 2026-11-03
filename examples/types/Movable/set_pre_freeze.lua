-- Make the player immune to freezing.
players[1]:set_pre_freeze(function(_, _, _)
    return true
end)
