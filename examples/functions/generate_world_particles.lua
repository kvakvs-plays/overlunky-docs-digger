-- Make smoke fall from the floor tile below the first player.
local x, y, layer = get_position(players[1].uid)
generate_world_particles(PARTICLEEMITTER.FLOORFALLINGDUST_SMOKE, get_grid_entity_at(x, y - 1, layer))
