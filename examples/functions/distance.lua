-- Print how far the first player is from the first caveman, in tiles.
local target = get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1]
if target then print(distance(players[1].uid, target)) end
