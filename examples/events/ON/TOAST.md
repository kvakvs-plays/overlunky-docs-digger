**Return an empty string to swallow a message.** Compare the text and return `""` for the toasts you do not want; return nothing to leave all others alone. The text is the localized string, so an exact comparison works only for the game language you tested.

```lua
local unwanted = "the exact toast text to hide"
set_callback(function(text)
    if text == unwanted then
        return ""
    end
end, ON.TOAST)
```

**Use a one-shot callback to rewrite the next toast.** Return a string to replace the text, and call `clear_callback()` inside to remove the callback after it ran once. Only the first script that returns a string decides; later ones just see the call.

```lua
set_callback(function(_)
    clear_callback()
    return "Replaced once"
end, ON.TOAST)
```
