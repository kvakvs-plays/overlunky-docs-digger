-- Show a message when the first floor tile of the level is destroyed.
local floor_uid = get_entities_by_type(ENT_TYPE.FLOOR_GENERIC)[1]
set_on_destroy(floor_uid, function() toast("Floor destroyed") end)
