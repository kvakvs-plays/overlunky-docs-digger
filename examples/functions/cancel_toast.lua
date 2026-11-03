-- Replace a toast that is already shown: cancel it and show the new one one frame later.
cancel_toast()
set_timeout(function() toast("Hello") end, 1)
