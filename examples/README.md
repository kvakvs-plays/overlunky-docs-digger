# Examples

Examples that the HTML reference shows next to a symbol, in addition to overlunky's `docs/examples/` (which stays
the first source and is not changed by docs-digger). `site.bat` passes this directory as `DD_SITE_DIGGER_EXAMPLES`.

## Layout

`<kind>/<name>.lua` or `<kind>/<name>.md`, where a `.` in the name becomes a directory:

| Kind        | Symbol                                    | File                         |
| ----------- | ----------------------------------------- | ---------------------------- |
| `functions` | global function `spawn_entity`            | `functions/spawn_entity.lua` |
| `functions` | library function `os.clock`               | `functions/os/clock.lua`     |
| `types`     | usertype `Entity`                         | `types/Entity.lua`           |
| `types`     | member `Entity:destroy` / `Entity.x`      | `types/Entity/destroy.lua`   |
| `enums`     | enum `ENT_TYPE`                           | `enums/ENT_TYPE.lua`         |
| `events`    | event `ON.LEVEL`                          | `events/ON/LEVEL.lua`        |
| `globals`   | global variable `state`                   | `globals/state.lua`          |
| `text`      | `## ` section of a hand-written text page | `text/json.md`               |

Lua names have at most one `.` (an owner and a member), so the tree is never deeper than `<kind>/<owner>/<member>`.
The kind directory exists because some names differ only in case (`Door` type and `door` function, `prng` global
and `PRNG` type), and such files cannot share a directory on Windows.

- A `.lua` file becomes a Lua code block; a `.md` file is shown as Markdown (text around the code, several blocks).
  A symbol may have both; overlunky's examples come first.
- A file that names nothing on the pages fails the site build with its path, so a renamed or removed symbol does
  not lose its example silently: move the file with the symbol.
- Write examples that run as they are in the in-game console where possible, and check them with luacheck.
