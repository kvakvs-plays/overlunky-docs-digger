-- Get an entity from its uid and destroy the first rock.
local uid = get_entities_by_type(ENT_TYPE.ITEM_ROCK)[1]
if uid then get_entity(uid):destroy() end
