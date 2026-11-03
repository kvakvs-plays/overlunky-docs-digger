-- Stop the player from attacking anyone by cancelling every attack.
players[1]:set_pre_attack(function(_, _)
    return true
end)
