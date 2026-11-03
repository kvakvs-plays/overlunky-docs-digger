-- Kill the first player when they take fire damage.
players[1]:set_post_damage(function(self, dealer, _, damage_flags)
    if damage_flags & DAMAGE_TYPE.FIRE ~= 0 then self:kill(true, dealer) end
end)
