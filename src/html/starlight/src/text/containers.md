Sometimes game variables and return of some functions will be of type `map`, `set`, `vector` etc. from the C++ Standard Library.

You don't really need to know much of this as they will behave similar to a lua table, even accept some table functions from the `table` library and support looping thru using `pair` function. You can also use them as parameter for functions that take `array`, Sol will happily convert them for you.

They come with some extra functionality:

Type | Name | Description
---- | ---- | -----------
bool | all:empty() | Returns true if container is empty, false otherwise
int | all:size() | Same as `#container`
any | vector:at(int index) | Same as `vector[index]`
any | span:at(int index) | Same as `span[index]`
any | set:at(int order) | Returns elements in order, it's not an index as sets don't have one
any | map:at(int order) | Returns elements in order, it's not an index as maps don't have one
int | vector:find(any value) | Searches for the value in vector, returns index of the item in vector or nil if not found, only available for simple values that are comparable
int | span:find(any value) | Searches for the value in span, returns index of the item in span or nil if not found, only available for simple values that are comparable
any | set:find(any key) | Searches for the value in set, returns the value itself or nil if not found, only available for simple values that are comparable
any | map:find(any key) | Searches for the key in map, returns the value itself or nil if not found, only available for simple keys that are comparable
nil | vector:erase(int index) | Removes element at given index, the rest of elements shift down so that the vector stays contiguous
nil | set:erase(any key) | Removes element from set
nil | map:erase(any key) | Removes element from map
nil | vector:clear() | Removes all elements from vector
nil | set:clear() | Removes all elements from set
nil | map:clear() | Removes all elements from map
nil | vector:insert(int index, any element) | Inserts element at given index, the rest of elements shift up in index
nil | set:insert(int order, any key) | The order param doesn't acutally matter and can be set to nil
nil | map:insert(any key, any value)? | unsure, probably easier to just use `map[key] = value`
