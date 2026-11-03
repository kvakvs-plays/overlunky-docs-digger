-- Force a custom theme for the level that is about to load.
local theme = CustomTheme.new()
set_callback(function() force_custom_theme(theme) end, ON.PRE_LOAD_LEVEL_FILES)
