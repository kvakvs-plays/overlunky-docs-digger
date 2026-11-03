-- Draw a text wobbling slightly: rotate it for drawing, then rotate it back.
local tri = TextRenderingInfo.new("Hello", 0.001, 0.001, VANILLA_TEXT_ALIGNMENT.CENTER, VANILLA_FONT_STYLE.ITALIC)
set_callback(function(ctx) tri:rotate(0.1) ctx:draw_text(tri, Color.white()) tri:rotate(-0.1) end, ON.RENDER_POST_HUD)
