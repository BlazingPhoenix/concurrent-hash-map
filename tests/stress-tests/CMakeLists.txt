add_executable(stress_unchecked stress_unchecked.cpp)
target_link_libraries(stress_unchecked
        PRIVATE concurrent_hash_map
        PRIVATE pcg
)

add_test(NAME stress_unchecked COMMAND stress_unchecked)

add_executable(stress_checked
        stress_checked.cpp
)
target_link_libraries(stress_checked
        PRIVATE concurrent_hash_map
        PRIVATE pcg
)
add_test(NAME stress_checked COMMAND stress_checked)

find_package(Boost COMPONENTS system thread REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${Libcuckoo_INCLUDE_DIRS})
add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark
        PRIVATE ${Boost_SYSTEM_LIBRARY}
        PRIVATE ${Boost_THREAD_LIBRARY}
        PRIVATE concurrent_hash_map
        PRIVATE pcg
)
add_test(NAME benchmark COMMAND benchmark)
