-- Reuse the fog, camera and other effects of the dwelling theme in another theme.
state.level_gen.themes[THEME.ICE_CAVES]:set_pre_spawn_effects(function()
    state.level_gen.themes[THEME.DWELLING]:spawn_effects()
    return true
end)
