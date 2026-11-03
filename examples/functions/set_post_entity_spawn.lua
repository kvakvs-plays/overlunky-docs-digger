-- Make every newly spawned snake invisible.
set_post_entity_spawn(function(ent)
    ent.flags = set_flag(ent.flags, ENT_FLAG.INVISIBLE)
end, SPAWN_TYPE.ANY, MASK.MONSTER, ENT_TYPE.MONS_SNAKE)
