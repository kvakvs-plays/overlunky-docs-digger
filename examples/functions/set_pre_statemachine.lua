-- Freeze the first snake by skipping its state machine update every frame.
local uid = get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1]
set_pre_statemachine(uid, function() return true end)
