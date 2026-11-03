-- Only shake the camera when the player has not disabled screen shake in the game settings.
if get_setting(GAME_SETTING.SCREEN_SHAKE) ~= 0 then state.camera.shake_amplitude = 3 end
