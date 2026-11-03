-- Read back the saved data string when a save is loaded.
set_callback(function(ctx)
    print(ctx:load() or "no save data")
end, ON.LOAD)
