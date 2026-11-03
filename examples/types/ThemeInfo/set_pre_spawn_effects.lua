-- Replace the spawn effects of the jungle with those of the dwelling (returning true skips the original).
state.level_gen.themes[THEME.JUNGLE]:set_pre_spawn_effects(function()
    state.level_gen.themes[THEME.DWELLING]:spawn_effects()
    return true
end)
