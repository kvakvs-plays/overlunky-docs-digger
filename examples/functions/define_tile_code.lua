-- Define a new tile code and spawn a rock whenever the level files contain it.
define_tile_code("my_rock")
set_pre_tile_code_callback(function(x, y, l) spawn(ENT_TYPE.ITEM_ROCK, x, y, l, 0, 0) return true end, "my_rock")
