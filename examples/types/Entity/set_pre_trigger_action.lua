-- Push the holder back whenever a held weapon is triggered.
local gun = get_entity(get_entities_by_type(ENT_TYPE.ITEM_SHOTGUN)[1])
gun:set_pre_trigger_action(function(_, holder) holder.velocityx = holder.velocityx - 0.2 end)
