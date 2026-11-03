-- Give the first ladder a normal map from another texture.
local ladder = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_LADDER)[1])
ladder.rendering_info:set_normal_map_texture(TEXTURE.DATA_TEXTURES_FLOOR_CAVE_0)
