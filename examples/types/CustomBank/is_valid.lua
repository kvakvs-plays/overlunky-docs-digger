-- Load a sound bank and check that it was loaded successfully.
local bank = load_bank("sounds.bank", 0)
print(bank ~= nil and bank:is_valid())
