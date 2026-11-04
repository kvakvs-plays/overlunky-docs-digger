**Serialize one table with `json` and write it once.** `save_ctx:save()` takes a single string, so keep all your persistent state in one table and encode it here. Guard the encoding with `pcall` so an unencodable value cannot break saving.

```lua
local data = { runs = 0 } -- filled in by your ON.LOAD callback
set_callback(function(save_ctx)
    local ok, text = pcall(json.encode, data)
    if ok then
        save_ctx:save(text)
    else
        print("could not save: " .. tostring(text))
    end
end, ON.SAVE)
```
