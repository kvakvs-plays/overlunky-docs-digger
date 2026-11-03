-- Set every web in the level on fire for 100 frames.
for _, uid in ipairs(get_entities_by_type(ENT_TYPE.ITEM_WEB)) do
    get_entity(uid):light_on_fire(100)
end
