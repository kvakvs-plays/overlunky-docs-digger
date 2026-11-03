-- Make the player immune to fire damage by skipping the damage function.
players[1]:set_pre_damage(function(_, _, _, damage_flags)
    if damage_flags & DAMAGE_TYPE.FIRE ~= 0 then return true end
end)
