-- Silence the item pick-up sound by stopping it as soon as it is created.
set_vanilla_sound_callback(VANILLA_SOUND.SHARED_PICK_UP, VANILLA_SOUND_CALLBACK_TYPE.CREATED, function(sound)
    sound:stop()
end)
