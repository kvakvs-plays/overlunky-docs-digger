-- Make the first player face left after the players are spawned in the dwelling.
state.level_gen.themes[THEME.DWELLING]:set_post_spawn_players(function()
    players[1].flags = set_flag(players[1].flags, ENT_FLAG.FACING_LEFT)
end)
