**Compare against `hash_to_stringid`, not a literal number.** The parameter is a STRINGID of the death title, and ids of vanilla strings should not be hard coded. Convert the string hash you look for and compare, or read the title with `get_string`.

```lua
set_callback(function(stringid)
    print("death title:", get_string(stringid))
    if stringid == hash_to_stringid(0x4437f3f7) then -- hash from strings00_hashed.str
        print("died to this specific cause")
    end
end, ON.DEATH_MESSAGE)
```
