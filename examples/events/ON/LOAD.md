**Decode defensively and fall back to defaults.** `load_ctx:load()` returns an empty result when the script has never saved, and the stored text may be damaged, so wrap `json.decode` in `pcall`. `ON.LOAD` runs once when the script loads, before your other callbacks.

```lua
local data = { runs = 0 }
set_callback(function(load_ctx)
    local text = load_ctx:load()
    if text and text ~= "" then
        local ok, decoded = pcall(json.decode, text)
        if ok and type(decoded) == "table" then
            data = decoded
        end
    end
    print("runs so far:", data.runs)
end, ON.LOAD)
```
