-- Get the mount a player is riding (or the player itself if not mounted).
local mount = players[1]:topmost_mount()
print(mount.uid)
