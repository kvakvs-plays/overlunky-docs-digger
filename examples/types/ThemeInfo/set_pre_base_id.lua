-- Let the game treat the volcana as the ice caves when it looks up the base theme.
state.level_gen.themes[THEME.VOLCANA]:set_pre_base_id(function()
    return THEME.ICE_CAVES
end)
