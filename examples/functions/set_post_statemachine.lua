-- Keep the first snake facing left no matter what its AI decides.
local uid = get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1]
set_post_statemachine(uid, function(self) self.flags = set_flag(self.flags, ENT_FLAG.FACING_LEFT) end)
