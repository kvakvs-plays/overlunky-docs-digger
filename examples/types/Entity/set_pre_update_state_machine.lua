-- Freeze a player's horizontal speed to what it was before the game updated it, undoing any change.
local ent = players[1]
ent:set_pre_update_state_machine(function(self) self.user_data = { vx = self.velocityx } end)
ent:set_post_update_state_machine(function(self) self.velocityx = self.user_data.vx end)
