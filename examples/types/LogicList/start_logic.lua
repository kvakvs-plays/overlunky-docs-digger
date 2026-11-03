-- Start the magmaman spawn logic in a level that does not run it yet; its list of spawn positions starts empty.
local logic = state.logic.magmaman_spawn or state.logic:start_logic(LOGIC.MAGMAMAN_SPAWN)
print(#logic.magmaman_positions)
