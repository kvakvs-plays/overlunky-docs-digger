-- Run code after the theme's flags are initialized, e.g. to prevent dark levels.
local theme = CustomTheme:new(100, THEME.VOLCANA)
theme:post(THEME_OVERRIDE.INIT_FLAGS, function() state.level_flags = clr_flag(state.level_flags, 18) end)
