-- Load an FMOD bank file and check that it is valid before using it.
local bank = load_bank("my_bank.bank", 0)
if bank ~= nil and bank:is_valid() then print("loaded") end
