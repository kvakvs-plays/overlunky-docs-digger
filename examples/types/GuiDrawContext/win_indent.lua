-- Indent a block of text in the script options window, then restore the indentation.
register_option_callback("", {}, function(ctx)
    ctx:win_indent(20) ctx:win_text("Indented text") ctx:win_indent(-20)
end)
