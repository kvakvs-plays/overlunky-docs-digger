-- Play a vanilla sound and get the audio object back, so it can be adjusted later.
local audio = get_sound(VANILLA_SOUND.SHARED_PICK_UP):play(true)
audio:set_pause(false)
