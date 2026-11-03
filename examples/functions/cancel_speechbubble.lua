-- Replace a speech bubble that is already shown: cancel it and say the new line one frame later.
cancel_speechbubble()
set_timeout(function() say(players[1].uid, "Hello", 0, false) end, 1)
