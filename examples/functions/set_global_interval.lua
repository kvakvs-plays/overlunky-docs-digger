-- Every second (60 frames) print where the first player is, even when the game is paused.
set_global_interval(function()
    print(get_position(players[1].uid))
end, 60)
