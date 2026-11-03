-- Find out which back item the first player wears, if any.
local backitem = get_entity(players[1]:worn_backitem())
if backitem then print(backitem.type.id == ENT_TYPE.ITEM_JETPACK) end
