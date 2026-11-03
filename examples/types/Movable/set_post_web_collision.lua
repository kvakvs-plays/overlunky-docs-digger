-- Print when the player collides with a web.
players[1]:set_post_web_collision(function(self, _)
    print("webbed: " .. self.uid)
end)
