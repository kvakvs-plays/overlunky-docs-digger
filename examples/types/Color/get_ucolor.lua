-- Draw a white line on screen: GuiDrawContext functions take a packed color.
set_callback(function(ctx) ctx:draw_line(-0.5, 0, 0.5, 0, 2, Color.white():get_ucolor()) end, ON.GUIFRAME)
