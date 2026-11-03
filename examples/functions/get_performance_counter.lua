-- Measure how many seconds passed between two frames, for frame-rate independent animation.
local last = get_performance_counter()
set_callback(function() print((get_performance_counter() - last) / get_performance_frequency()) end, ON.GUIFRAME)
