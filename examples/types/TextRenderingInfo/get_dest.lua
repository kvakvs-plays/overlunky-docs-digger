-- Get the destination letters of a text and print the left edge of the first one.
local tri = TextRenderingInfo.new("Hello", 0.001, 0.001, VANILLA_TEXT_ALIGNMENT.CENTER, VANILLA_FONT_STYLE.BOLD)
print(tri:get_dest()[1]:get_quad():get_AABB().left)
