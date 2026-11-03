-- Start a sound paused, then resume it.
local sound = get_sound(VANILLA_SOUND.ENEMIES_OLMEC_STOMP):play(true)
sound:set_pause(false)
