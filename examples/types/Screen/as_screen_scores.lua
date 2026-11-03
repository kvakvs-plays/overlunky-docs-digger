-- Get the scores-specific view of the screen to read how far its animation has progressed.
set_post_render_screen(SCREEN.SCORES, function(screen, _)
    print(screen:as_screen_scores().animation_timer)
end)
