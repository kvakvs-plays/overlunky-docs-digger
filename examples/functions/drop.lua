-- Make the first player let go of whatever they hold, then destroy that item.
local held = get_entity(players[1].holding_uid)
if held then drop(players[1].uid, held.uid) held:destroy() end
