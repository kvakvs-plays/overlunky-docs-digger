-- Print a message after the first player dies.
players[1]:set_post_kill(function(self) print(self.uid, "died") end)
