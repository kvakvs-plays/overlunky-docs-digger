**Fields are nil when the logic is not running.** Every `LogicList` field is only set when the game started that logic in the current level.
Always test for nil before reading it, and do not cache the object across levels.

```lua
local dice = state.logic.diceshop
if dice then
    print("dice shop balance", dice.balance)
end
```

**Point a logic at your own entities.** Logic fields can be changed once the level is running, for example the boss of a dice shop: `boss_uid` (`-1` for none) after you replaced or spawned the shopkeeper.

```lua
set_timeout(function()
    local dice = state.logic.diceshop
    if not dice then return end
    local keeper = get_entities_by(ENT_TYPE.MONS_SHOPKEEPER, MASK.MONSTER, LAYER.FRONT)[1]
    dice.boss_uid = keeper or -1
end, 1)
```

**Create a logic that the level lacks.** `start_logic` returns the new logic, which then works like a vanilla one. The magmaman spawner only acts on positions you register with `add_spawn`, so scan the level for lava and add each cell.

```lua
local logic = state.logic.magmaman_spawn or state.logic:start_logic(LOGIC.MAGMAMAN_SPAWN)
local left, top, right, bottom = get_bounds()
for x = math.floor(left + 0.5), math.floor(right - 0.5) do
    for y = math.floor(bottom + 0.5), math.floor(top - 0.5) do
        local _, lava = get_liquids_at(x, y, LAYER.FRONT)
        if lava > 0 then logic:add_spawn(x, y) end
    end
end
```
