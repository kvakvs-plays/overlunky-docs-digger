-- Make the first player ignore all whip damage.
set_on_damage(players[1].uid, function(_, _, _, damage_flags)
    if test_mask(damage_flags, DAMAGE_TYPE.WHIP) then return true end
end)
