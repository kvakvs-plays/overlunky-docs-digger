-- Play a sound and tell it that the collision happened on material 2.
local sound = get_sound(VANILLA_SOUND.ENEMIES_OLMITE_ARMOR_BREAK):play()
sound:set_parameter(VANILLA_SOUND_PARAM.COLLISION_MATERIAL, 2)
