-- luacheck settings for the examples: they are script snippets, so the API they use (`get_entity`, `ENT_TYPE`, ...)
-- is the game's globals, which luacheck does not know; everything else (syntax, unused or shadowed locals) is checked.
std = "lua54"
max_line_length = 120
ignore = {
    "113", -- accessing undefined variable: the overlunky API globals
}
