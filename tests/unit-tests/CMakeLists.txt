add_executable(unit_tests
  test_constructor.cc
#  test_hash_properties.cc
#  test_heterogeneous_compare.cc
  test_iterator.cc
  test_noncopyable_types.cc
#  test_resize.cc
  test_runner.cc
#  test_user_exceptions.cc
#  test_locked_table.cc
#  test_libcuckoo_bucket_container.cc
  unit_test_util.cc
  unit_test_util.hh
)

target_link_libraries(unit_tests
  PRIVATE concurrent_hash_map
  PRIVATE catch
)

add_test(NAME unit_tests COMMAND unit_tests)
