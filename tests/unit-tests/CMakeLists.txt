add_executable(unit_tests
        test_constructor.cpp
        test_hash_properties.cpp
        test_heterogeneous_compare.cpp
        test_iterator.cpp
        test_noncopyable_types.cpp
        test_resize.cpp
        test_runner.cpp
        test_user_exceptions.cpp
        test_locked_table.cpp
        test_libcuckoo_bucket_container.cpp
        unit_test_util.cpp
        unit_test_util.hpp
)

target_link_libraries(unit_tests
  PRIVATE concurrent_hash_map
  PRIVATE catch
)

add_test(NAME unit_tests COMMAND unit_tests)
