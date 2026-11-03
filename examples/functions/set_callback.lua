-- Print a message every time a new level is loaded.
set_callback(function() print("Entered level " .. state.level) end, ON.LEVEL)
