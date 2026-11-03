-- Add a command that can be called from the console as spawn_snake().
register_console_command("spawn_snake", function() spawn(ENT_TYPE.MONS_SNAKE, 10, 100, LAYER.FRONT, 0, 0) end)
