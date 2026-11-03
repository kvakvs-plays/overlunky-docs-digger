-- Store a small piece of data in the script save when the game saves.
set_callback(function(ctx)
    ctx:save(string.format("{\"level\": %d}", state.level))
end, ON.SAVE)
