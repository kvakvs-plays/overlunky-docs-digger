-- Stop listening to a vanilla sound after it was registered.
local id = set_vanilla_sound_callback(VANILLA_SOUND.ITEMS_BACKPACK_WARN, VANILLA_SOUND_CALLBACK_TYPE.STARTED, print)
clear_vanilla_sound_callback(id)
