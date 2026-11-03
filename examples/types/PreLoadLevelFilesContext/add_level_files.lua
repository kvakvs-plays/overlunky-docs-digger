-- Load an additional .lvl file (relative to Data/Levels) next to the vanilla ones.
set_callback(function(ctx)
    ctx:add_level_files({ "custom_rooms.lvl" })
end, ON.PRE_LOAD_LEVEL_FILES)
