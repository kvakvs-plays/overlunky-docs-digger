-- Draw a texture sheet as an image on the screen.
local tex = TEXTURE.DATA_TEXTURES_BG_ICE_0
set_callback(function(ctx) ctx:draw_image(tex, -0.5, 0.5, 0.5, -0.5, 0, 0, 1, 1, 0xffffffff) end, ON.GUIFRAME)
