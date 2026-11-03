-- Convert performance counter ticks to seconds.
local t0 = get_performance_counter()
print((get_performance_counter() - t0) / get_performance_frequency())
