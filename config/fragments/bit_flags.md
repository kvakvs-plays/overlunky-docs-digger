Bit flags: each value is one bit (`{{a}}` is `1 << {{a_bit}}`) or a union of bits, so combine values with `|` and
test them with `&`. The `*_mask` functions take these values; `test_mask` is true when any of the given bits is set.
The `*_flag` functions take a bit number counted from 1 instead (`{{a}}` is bit {{a_number}}). All of them return the
new number and leave their argument unchanged.

```lua
local flags = set_mask(0, {{a}} | {{b}}) -- both on
flags = clr_mask(flags, {{b}}) -- {{b}} off
flags = flip_mask(flags, {{b}}) -- {{b}} on again
if test_mask(flags, {{a}}) then
    flags = clr_flag(flags, {{a_number}}) -- {{a}} off, by bit number
end
flags = set_flag(flags, {{a_number}}) -- {{a}} on
flags = flip_flag(flags, {{a_number}}) -- {{a}} off
```
