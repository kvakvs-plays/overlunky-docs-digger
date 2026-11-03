-- Group widgets into a collapsible section of the script options window.
register_option_callback("", {}, function(ctx)
    ctx:win_section("Advanced", function() ctx:win_text("Hidden until opened") end)
end)
