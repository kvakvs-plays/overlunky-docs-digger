**Return a new page list to replace the chapter.** The returned numbers become the pages in the order given, left page first, then right page. Numbers above 99 cannot clash with real game pages and are created as empty story pages.

```lua
set_callback(function(chapter, _pages)
    if chapter == JOURNALUI_PAGE_SHOWN.PEOPLE then
        return { 101, 102, 103, 104 } -- two spreads of custom pages
    end
end, ON.POST_LOAD_JOURNAL_CHAPTER)
```

**Keep your page numbers apart per chapter.** Give each chapter its own block (for example 100 numbers each) so that `RENDER_POST_JOURNAL_PAGE` can later tell from `page.page_number` which chapter and entry a page belongs to. Returning nothing loads the chapter normally.

```lua
local BLOCK <const> = { [JOURNALUI_PAGE_SHOWN.PEOPLE] = 100, [JOURNALUI_PAGE_SHOWN.ITEMS] = 200 }
set_callback(function(chapter, _pages)
    local base = BLOCK[chapter]
    if not base then return end
    local pages = {}
    for i = 1, 6 do pages[i] = base + i end
    return pages
end, ON.POST_LOAD_JOURNAL_CHAPTER)
```
