-- Make all aliens slower by changing their entity type definition.
local db = get_type(ENT_TYPE.MONS_ALIEN)
db.max_speed = 0.02
