# concurrent-hash-map
STL compatible implementation of hash map optimized for concurrent access
# usage example
```
#include "concurrent_hash_map.hpp"
#include <string>

concurrent_unordered_map<std::string, int> m;
m.insert(std::make_pair("abc", 123));
assert(m["abc"] == 123);
```
Also you need to have boost at include path and link with boost system library.

# license
This library is released under the Boost Software License (please see http://boost.org/LICENSE_1_0.txt or the accompanying LICENSE_1_0.txt file for the full text.
