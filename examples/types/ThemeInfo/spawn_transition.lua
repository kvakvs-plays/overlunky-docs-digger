-- Spawn the volcana transition tunnel in place of the ice caves one (returning true skips the original).
state.level_gen.themes[THEME.ICE_CAVES]:set_pre_spawn_transition(function()
    state.level_gen.themes[THEME.VOLCANA]:spawn_transition()
    return true
end)
