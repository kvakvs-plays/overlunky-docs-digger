**The speaker can be missing.** `speaking_entity` is optional, so check it before reading `type`. Use the entity type to react only to the characters you care about, here shopkeepers.

```lua
set_callback(function(speaking_entity, _text)
    if speaking_entity and speaking_entity.type.id == ENT_TYPE.MONS_SHOPKEEPER then
        print("a shopkeeper is speaking", speaking_entity.uid)
    end
end, ON.SPEECH_BUBBLE)
```

**Return a string to rewrite or silence the bubble.** Returning `""` creates no bubble at all, returning nothing keeps the original line. This applies to bubbles made by `say` too.

```lua
set_callback(function(speaking_entity, _)
    if speaking_entity and speaking_entity.type.id == ENT_TYPE.MONS_SHOPKEEPER then
        return "" -- stay quiet
    end
end, ON.SPEECH_BUBBLE)
```
