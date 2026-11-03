-- Choose where the players spawn in the dwelling by setting the spawn position before they are placed.
state.level_gen.themes[THEME.DWELLING]:set_pre_spawn_players(function()
    state.level_gen.spawn_x, state.level_gen.spawn_y = 10.5, 100.0
end)
