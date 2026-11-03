-- Define an extra spawn that places one queen bee per level in the front layer wherever allowed.
define_extra_spawn(function(x, y, layer) spawn_entity(ENT_TYPE.MONS_QUEENBEE, x, y, layer, 0, 0) end,
    function() return true end, 1, 0)
