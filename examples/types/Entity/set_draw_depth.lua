-- Draw an entity behind others by giving it a higher draw depth.
local deco = get_entity(spawn_entity(ENT_TYPE.BG_LEVEL_BACKWALL, 10, 100, LAYER.FRONT, 0, 0))
deco:set_draw_depth(45)
