add_executable(stress_unchecked stress_unchecked.cc)
target_link_libraries(stress_unchecked
        PRIVATE concurrent_hash_map
        PRIVATE pcg
)

add_test(NAME stress_unchecked COMMAND stress_unchecked)

add_executable(stress_checked
        stress_checked.cc
        )
target_link_libraries(stress_checked
        PRIVATE concurrent_hash_map
        PRIVATE pcg
        )

add_test(NAME stress_checked COMMAND stress_checked)