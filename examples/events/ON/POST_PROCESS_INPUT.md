**Filter menu input before the game reacts to it.** The menu buttons are written during input processing, so a `POST_PROCESS_INPUT` callback can clear or swap bits in `game_manager.game_props.input_menu` and the menus will behave as if the player never pressed them. Clear only the bits you need with `& ~`.

```lua
set_callback(function()
    local props = game_manager.game_props
    props.input_menu = props.input_menu & ~MENU_INPUT.BACK
end, ON.POST_PROCESS_INPUT)
```
