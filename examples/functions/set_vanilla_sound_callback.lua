-- Mute the pick up sound as soon as it is created.
set_vanilla_sound_callback(VANILLA_SOUND.SHARED_PICK_UP, VANILLA_SOUND_CALLBACK_TYPE.CREATED, function(sound)
    sound:set_volume(0)
    sound:stop()
end)
