## Read this first

- We try not to make breaking changes to the API, but some stupid errors or new stuff that was guessed wrong may have to be changed. Sorry!
- If you encounter something that doesn't seem quite right, please raise your voice instead of conforming to it outright.
- This doc is generated from the C++ sources by [docs-digger](https://github.com/kvakvs-plays/overlunky-docs-digger), which reads the actual bindings, so parameter and callback types come from the code. If something still looks wrong, it may be a weird error in the API itself.
- This doc doesn't have a lot of examples, that's why we have [examples/](https://github.com/spelunky-fyi/overlunky/tree/main/examples). There are also [sample mods](https://spelunky.fyi/mods/?q=sample) for things that make more sense in Playlunky.
- This doc and the examples are written for a person who already knows [how to program in Lua](http://lua-users.org/wiki/TutorialDirectory).
- This doc doesn't cover how to actually load scripts. Check the [README](https://github.com/spelunky-fyi/overlunky/#scripts) for instructions.
- This doc is up to date for the Overlunky [WHIP build](https://github.com/spelunky-fyi/overlunky/releases/tag/whip) and Playlunky [nightly build](https://github.com/spelunky-fyi/Playlunky/releases/tag/nightly). If you're using a stable release from the past, you might find some things here don't work.
- Use `Overlunky.exe --console` command line switch to keep the Overlunky terminal open for better debug prints. Playlunky will also print the messages to terminal (even from Overlunky) if ran with the `-console` switch.
- Testing functions, exploring the types, and most inline Lua examples in here can be used in the in-game REPL console, default opened with the tilde key or regional equivalent. Full script examples can be found in the Overlunky Scripts menu if the script bundle is installed.

## External Function Library

If you use a text editor or IDE that has a Lua linter, download the API definition files below, place them in a folder of your choice and specify that folder as an "external function library". For example [VSCode](https://code.visualstudio.com/) with the [Lua Extension](https://marketplace.visualstudio.com/items?itemName=sumneko.lua) (lua-language-server) offers this feature: add the folder to `Lua.workspace.library`. This gives you auto-completion of API functions along with linting, typed callback parameters included.

The files come in two dialects. **LuaLS** is for lua-language-server and uses its newer annotations (`T?` for values that may be nil, tuple types, operators, a callback typed per event). **EmmyLua** is the classic, more limited dialect for older IDE plugins (`T|nil`, `table` for tuples, no operators). Each file stays under the 500 KB that lua-language-server loads by default.
