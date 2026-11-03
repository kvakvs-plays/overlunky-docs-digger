-- Run code after the traps of every dwelling level have been spawned.
state.level_gen.themes[THEME.DWELLING]:set_post_spawn_traps(function()
    print("traps spawned")
end)
