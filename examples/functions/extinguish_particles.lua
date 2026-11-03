-- Start a particle effect on the first player and stop it again after one second.
local emitter = generate_world_particles(PARTICLEEMITTER.PORTAL_WARP, players[1].uid)
set_timeout(function() extinguish_particles(emitter) end, 60)
