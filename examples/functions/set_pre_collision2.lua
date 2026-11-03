-- Let the first player ignore collisions with rocks.
set_pre_collision2(players[1].uid, function(_, other)
    if other.type.id == ENT_TYPE.ITEM_ROCK then return true end
end)
