**Recognise your own pages by number, not by type.** Pages added through `POST_LOAD_JOURNAL_CHAPTER` are story pages, so `page_type` is always `JOURNAL_PAGE_TYPE.STORY` for them. Test `page.page_number` against the numbers you handed out and draw on top of the empty page.

```lua
set_callback(function(ctx, _page_type, page)
    if page and page.page_number > 100 and page.page_number < 200 then
        ctx:draw_text("Custom page " .. page.page_number, 0, 0, 0.001, 0.001,
            Color.white(), VANILLA_TEXT_ALIGNMENT.CENTER, VANILLA_FONT_STYLE.BOLD)
    end
end, ON.RENDER_POST_JOURNAL_PAGE)
```

**Check `page_type` before casting.** The `as_journal_page_*` casts return a typed page only for the matching type, so branch on `page_type` first and then use the fields of the cast page.

```lua
set_callback(function(_, page_type, page)
    if page_type == JOURNAL_PAGE_TYPE.JOURNAL_MENU and page then
        local menu = page:as_journal_page_journalmenu()
        print(menu.completion_badge.x, menu.completion_badge.y)
    end
end, ON.RENDER_POST_JOURNAL_PAGE)
```
