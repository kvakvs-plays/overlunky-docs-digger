-- Make the light of the first player more blue by raising its blue channel.
local light = create_illumination(Color.white(), 4, players[1].uid)
light.light1.blue = 2
