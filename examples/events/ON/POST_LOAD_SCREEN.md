**Runs for every screen, so filter.** It fires after each screen has loaded, including the return from the options menu, which is not a new level. Test `state.screen` and `state.screen_last` first.

```lua
set_callback(function()
    if state.screen_last == SCREEN.OPTIONS then return end
    if state.screen == SCREEN.LEVEL then
        print("fresh level, player 1 is", players[1].uid)
    end
end, ON.POST_LOAD_SCREEN)
```
