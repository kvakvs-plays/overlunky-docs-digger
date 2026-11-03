-- Add a magmaman spawn point at the first player's tile, starting the logic first if the level does not run it.
local x, y = get_position(players[1].uid)
local logic = state.logic.magmaman_spawn or state.logic:start_logic(LOGIC.MAGMAMAN_SPAWN)
logic:add_spawn(math.floor(x + 0.5), math.floor(y + 0.5))
