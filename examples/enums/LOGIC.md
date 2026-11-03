**Start a logic only when it is not running.** Each `state.logic` field is nil until the game starts that logic in the current level, so test the field first and call `start_logic` only if it is nil.
`start_logic` only builds the base object; fill in its fields yourself afterwards.

```lua
local logic = state.logic.magmaman_spawn or state.logic:start_logic(LOGIC.MAGMAMAN_SPAWN)
print(#logic.magmaman_positions, "registered spawn positions")
```

**Stop a logic you no longer want.** `stop_logic` takes the `LOGIC` value, so a level script can switch off behaviour that the theme would otherwise run.

```lua
if state.logic.ghost_trigger then
    state.logic:stop_logic(LOGIC.GHOST)
end
```

**Logic activity tells you about the level.** A running logic is a cheap way to ask what the current level contains: the ouroboros camera logic means the ouroboros is in the level, `diceshop` means a dice shop was generated.

```lua
local has_dice_shop = state.logic.diceshop ~= nil
local has_ouroboros = state.logic.ouroboros ~= nil
print(has_dice_shop, has_ouroboros)
```
