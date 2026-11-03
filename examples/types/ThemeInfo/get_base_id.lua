-- Get the logical base theme of the current level (special levels report the theme they belong to).
local base = state.current_theme:get_base_id()
print(base == THEME.TIDE_POOL)
