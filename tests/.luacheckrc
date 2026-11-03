-- luacheck settings for the generated definition files (spel2*.lua): they only declare globals, tables and empty
-- functions for the language server, so defining globals, unused arguments and fields added to the standard
-- library tables (`io.open_data`) are expected.
std = "lua54"
allow_defined_top = true
max_line_length = false
ignore = {
    "122", -- setting read-only field of a global: overlunky replaces some standard functions (`os.date`, `io.type`)
           -- with sandboxed versions, which are declared with their docs
    "131", -- unused implicitly defined global variable: declarations are never used in the file itself
    "142", -- setting undefined field of a global variable: `function io.open_data()` extends the io table
    "212", -- unused argument: function bodies are empty
}
