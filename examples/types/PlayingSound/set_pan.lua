-- Play a sound panned fully to the left speaker.
local sound = get_sound(VANILLA_SOUND.ENEMIES_OLMEC_STOMP):play()
sound:set_pan(-1)
