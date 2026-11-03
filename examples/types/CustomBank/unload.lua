-- Unload a sound bank once it is no longer needed.
local bank = load_bank("sounds.bank", 0)
if bank and bank:is_valid() then bank:unload() end
