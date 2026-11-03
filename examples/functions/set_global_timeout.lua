-- Show a message after one second, even if the game is paused in the meantime.
set_global_timeout(function() toast("One second passed") end, 60)
