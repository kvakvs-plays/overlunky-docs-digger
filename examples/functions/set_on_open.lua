-- Show a message whenever a crate is opened.
set_post_entity_spawn(function(crate)
    set_on_open(crate.uid, function() toast("Crate opened") end)
end, SPAWN_TYPE.ANY, MASK.ITEM, ENT_TYPE.ITEM_CRATE)
