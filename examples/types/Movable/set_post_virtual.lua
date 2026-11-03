-- Run code after every state machine update of the player.
players[1]:set_post_virtual(ENTITY_OVERRIDE.UPDATE_STATE_MACHINE, function(self)
    print(self.state, self.last_state)
end)
