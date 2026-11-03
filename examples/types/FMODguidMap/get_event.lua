-- Look up a custom FMOD event by its path, using a GUIDs.txt exported from FMOD Studio.
local event = create_fmod_guid_map("GUIDs.txt"):get_event("event:/UI/Cancel")
print(event)
