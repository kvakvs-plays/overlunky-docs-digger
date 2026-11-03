-- Remove every snake from the level.
for _, uid in ipairs(get_entities_by_type(ENT_TYPE.MONS_SNAKE)) do
    get_entity(uid):destroy()
end
