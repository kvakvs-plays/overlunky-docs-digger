**Return `true` and open something else.** The chapter is not loaded when any script returns `true`, which lets you redirect to a different chapter with `show_journal`. Redirect to another chapter than the one you intercept.

```lua
set_callback(function(chapter)
    if chapter == JOURNALUI_PAGE_SHOWN.JOURNAL then
        show_journal(JOURNALUI_PAGE_SHOWN.STORY, 2)
        return true
    end
end, ON.PRE_LOAD_JOURNAL_CHAPTER)
```

**Return nothing to just prepare.** The callback is also a hook that runs right before a chapter is built, for example to adjust game state that the pages read. Compare against `JOURNALUI_PAGE_SHOWN` to limit it to the chapters you care about.

```lua
set_callback(function(chapter)
    if chapter == JOURNALUI_PAGE_SHOWN.RECAP or chapter == JOURNALUI_PAGE_SHOWN.DEATH then
        print("journal chapter", chapter, "is about to load")
    end
end, ON.PRE_LOAD_JOURNAL_CHAPTER)
```
