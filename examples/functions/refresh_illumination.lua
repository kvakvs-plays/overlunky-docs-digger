-- Add a light to the first player and refresh it every frame so changes to it show.
local light = create_illumination(Color.white(), 7.0, players[1].uid)
set_callback(function() light.brightness = 0.85 refresh_illumination(light) end, ON.GAMEFRAME)
