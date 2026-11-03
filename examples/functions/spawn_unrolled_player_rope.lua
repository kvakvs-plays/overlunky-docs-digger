-- Spawn an already unrolled rope above the first player, using the first player texture.
local x, y, layer = get_position(players[1].uid)
spawn_unrolled_player_rope(x, y + 2, layer, state.items.player_select[1].texture)
