-- Register a sprite sheet of 128x128 tiles and draw the first player with it.
local def = TextureDefinition.new()
def.texture_path, def.width, def.height, def.tile_width, def.tile_height = "sheet.png", 256, 256, 128, 128
players[1]:set_texture(define_texture(def))
