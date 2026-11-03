-- Re-apply a custom texture to a floor tile after each of its updates.
local floor = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_GENERIC)[1])
floor:set_post_floor_update(function(self) self:set_texture(TEXTURE.DATA_TEXTURES_FLOOR_EGGPLANT_0) end)
