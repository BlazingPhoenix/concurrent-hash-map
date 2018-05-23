Concurrent hash map
===========
STL compatible implementation of hash map optimized for concurrent access. For more details look to proposal [D0652R0](http://apolukhin.github.io/papers/Concurrent%20and%20unordered.html)

Usage example
===========
```
#include "concurrent_hash_map.hpp"
#include <string>

std::concurrent_unordered_map<std::string, int> m;
m.emplace("abc", 123);
m.update("abc", 124);
assert(*m.find("abc") == 124);
```

Licence
===========
The prototype is based on libcuckoo originaly created by Carnegie Mellon University & Intel Corporation, but it have a different interface & functionality.

Copyright (C) 2013, Carnegie Mellon University and Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

---------------------------

The third-party libraries have their own licenses, as detailed in their source
files.
