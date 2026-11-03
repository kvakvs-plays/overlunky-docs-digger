-- Make the exit door of this level lead to world 1, level 2 in the dwelling theme.
set_door_target(get_entities_by_type(ENT_TYPE.FLOOR_DOOR_EXIT)[1], 1, 2, THEME.DWELLING)
