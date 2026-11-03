> **Known bug:** `PAUSE_SCREEN.EXIT` is declared as `1 << 31` in an `int64_t` enum. `1 << 31` is an `int`
> expression, so it overflows to `-2147483648` before it becomes 64 bits wide. In Lua that value also sets bits
> 32–63, not only bit 31. It should probably be `1LL << 31`. Until it is fixed, this page still lists `EXIT` as
> bit 31, with its value in decimal so that the sign is visible.
