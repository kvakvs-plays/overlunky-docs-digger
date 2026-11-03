-- Change the text of an existing text object (and its size and style) before drawing it.
local tri = TextRenderingInfo.new("Hello", 0.001, 0.001, VANILLA_TEXT_ALIGNMENT.CENTER, VANILLA_FONT_STYLE.ITALIC)
tri:set_text("PROTECT", 0.0025, 0.0025, VANILLA_TEXT_ALIGNMENT.CENTER, VANILLA_FONT_STYLE.BOLD)
set_callback(function(ctx) ctx:draw_text(tri, Color.white()) end, ON.RENDER_POST_HUD)
