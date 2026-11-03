-- Get the menu-specific view of the screen to read its fields after the main menu is drawn.
set_post_render_screen(SCREEN.MENU, function(screen, _)
    print(screen:as_screen_menu().cthulhu_timer)
end)
