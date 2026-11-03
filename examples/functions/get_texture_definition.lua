-- Look at the properties of a vanilla texture, e.g. to base a custom texture on it.
local def = get_texture_definition(TEXTURE.DATA_TEXTURES_FLOORMISC_0)
print(def.texture_path, def.width, def.height, def.tile_width, def.tile_height)
