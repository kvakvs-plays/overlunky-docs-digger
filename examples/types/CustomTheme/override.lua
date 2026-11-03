-- Create a theme based on Volcana that uses the Ice Caves' effects and background.
local theme = CustomTheme:new(100, THEME.VOLCANA)
theme:override(THEME_OVERRIDE.SPAWN_EFFECTS, THEME.ICE_CAVES)
theme:override(THEME_OVERRIDE.SPAWN_BACKGROUND, THEME.ICE_CAVES)
