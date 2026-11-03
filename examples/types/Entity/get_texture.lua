-- Give a new entity the same texture as another one.
local ent = players[1]
get_entity(spawn_entity(ENT_TYPE.FX_SHADOW, 0, 0, LAYER.FRONT, 0, 0)):set_texture(ent:get_texture())
