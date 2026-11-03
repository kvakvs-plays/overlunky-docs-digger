The following Lua libraries and their functions are available. You can read more about them in the [Lua documentation](https://www.lua.org/manual/5.4/manual.html#6). We're using Lua 5.4 with the [Sol C++ binding](https://sol2.readthedocs.io/en/latest/).

## io

`meta.unsafe` exposes all [standard library functions](https://www.lua.org/manual/5.4/manual.html#6.8) and removes basedir restrictions from the custom functions.

In safe mode (default) the following standard and custom functions are available:

- `io.type`
- `io.open_data`: like `io.open` but restricted to base directory `Mods/Data/modname`
- `io.open_mod`: like `io.open` but restricted to the mod directory

Safely opened files can be used normally through the `file:` handle. Files and folders opened in write mode are automatically created.

Also see [list_dir](#list_dir) and [list_data_dir](#list_data_dir).

## os

`meta.unsafe` exposes all [standard library functions](https://www.lua.org/manual/5.4/manual.html#6.9) and removes basedir restrictions from the custom functions.

In safe mode (default) the following standard and custom functions are available:

- `os.clock`
- `os.date`
- `os.difftime`
- `os.time`
- `os.remove_data`: like `os.remove` but restricted to base directory `Mods/Data/modname`
- `os.remove_mod`: like `os.remove` but restricted to the mod directory

## json

To save data in your mod it makes a lot of sense to use `json` to encode a table into a string and decode strings to table. For example this code that saves table and loads it back:

```lua
local some_mod_data_that_should_be_saved = {{
    kills = 0,
    unlocked = false
}}
set_callback(function(save_ctx)
    local save_data_str = json.encode(some_mod_data_that_should_be_saved)
    save_ctx:save(save_data_str)
end, ON.SAVE)

set_callback(function(load_ctx)
    local load_data_str = load_ctx:load()
    if load_data_str ~= "" then
        some_mod_data_that_should_be_saved = json.decode(load_data_str)
    end
end, ON.LOAD)
```

## inspect

This module is a great substitute for `tostring` because it can convert any type to a string and thus helps a lot with debugging. Use for example like this:

```lua
local look_ma_no_tostring = {
    number = 15,
    nested_table = {
        array = {
            1,
            2,
            4
        }
    }
}
message(inspect(look_ma_no_tostring))
--[[prints:
{
    number = 15,
    nested_table = {
        array = { 1, 2, 4 }
    }
}
]]
```

## format

This allows you to make strings without having to do a lot of `tostring` and `..` by placing your variables directly inside of the string. Use `F` in front of your string and wrap variables you want to print in `{}`, for example like this:

```lua
for _, player in players do
    local royal_title = nil
    if player:is_female() then
        royal_title = 'Queen'
    else
        royal_title = 'King'
    end
    local name = F'{player:get_name()} aka {royal_title} {player:get_short_name()}'
    message(name)
end
```

## Unsafe mode

Setting `meta.unsafe = true` enables the rest of the standard Lua libraries like unrestricted `io`, `os`, `ffi` and `debug`, loading dlls with require, `package.loadlib`, the [network functions](#Network-functions) and some [debug functions](#Debug-functions). Using unsafe scripts requires users to enable the option in the overlunky.ini file which is found in the Spelunky 2 installation directory.

## Modules

You can load modules with `require "mymod"` or `require "mydir.mymod"`, just put `mymod.lua` in the same directory the script is, or in `mydir/` to keep things organized.

Check the [Lua tutorial](http://lua-users.org/wiki/ModulesTutorial) or examples how to actually make modules.

You can also [import](#import) other loaded script mods to your own mod if they have `exports`.
