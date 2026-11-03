-- Declarations the C++ sources do not give: written in Lua by overlunky, or filled in by the scripts.
-- docs-digger puts this text at the start of spel2.lua (--lua-prelude); a global or function declared here
-- replaces the generated declaration of the same name.

---@class Meta
---@field name string
---@field version string
---@field description string
---@field author string

---Table of strings where you should set some script metadata shown in the UI and used by other scripts to find your
---script.
---@type Meta
meta = nil

---Create a global `exports` table and put stuff in it, and other scripts will be able to `import` your script like a
---library.
---@class Exports

---@type Exports
exports = nil

---The json library converts tables to json and json to tables.
---Check https://github.com/rxi/json.lua for more information.
---@class Json
---@field decode fun(str: string): table Decode a json string into a table
---@field encode fun(tbl: table): string Encode a table into a json string

---@type Json
json = nil

---Return any type of object or multiple objects as a debug string.
---@vararg any
---@return string
function inspect(...) end

---Formatting function, use e.g. as f"my_var = {my_var}"
---@param f_string string
---@return string
function f(f_string) end

---Formatting function, use e.g. as F"my_var = {my_var}"
---@param f_string string
---@return string
function F(f_string) end
