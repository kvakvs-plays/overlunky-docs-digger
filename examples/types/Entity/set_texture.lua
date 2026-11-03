-- Replace the texture of every border tile with a vanilla one.
for _, uid in ipairs(get_entities_by_type(ENT_TYPE.FLOOR_BORDERTILE)) do
    get_entity(uid):set_texture(TEXTURE.DATA_TEXTURES_BG_ICE_0)
end
