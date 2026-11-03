-- Drop a gold bar where the first snake dies.
local uid = get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1]
set_on_kill(uid, function(self) spawn(ENT_TYPE.ITEM_GOLDBAR, self.abs_x, self.abs_y, self.layer, 0, 0) end)
