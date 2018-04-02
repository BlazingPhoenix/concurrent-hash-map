#include <catch.hpp>

#include "unit_test_util.hpp"
#include <concurrent_hash_map/concurrent_hash_map.hpp>

// Checks that the alt index function returns a different bucket, and can
// recover the old bucket when called with the alternate bucket as the index.
template<class concurrent_map>
void check_key(size_t hashpower, const typename concurrent_map::key_type &key) {
    auto hashfn = typename concurrent_map::hasher();
    size_t hv = hashfn(key);
    auto partial = unit_test_internals_view::partial_key<concurrent_map>(hv);
    size_t bucket =
            unit_test_internals_view::index_hash<concurrent_map>(hashpower, hv);
    size_t alt_bucket = unit_test_internals_view::alt_index<concurrent_map>(
            hashpower, partial, bucket);
    size_t orig_bucket = unit_test_internals_view::alt_index<concurrent_map>(
            hashpower, partial, alt_bucket);

    REQUIRE(bucket != alt_bucket);
    REQUIRE(bucket == orig_bucket);
}

TEST_CASE("int alt index works correctly", "[hash properties]") {
    for (size_t hashpower = 10; hashpower < 15; ++hashpower) {
        for (int key = 0; key < 10000; ++key) {
            check_key<int_int_table>(hashpower, key);
        }
    }
}

TEST_CASE("string alt index works correctly", "[hash properties]") {
    for (size_t hashpower = 10; hashpower < 15; ++hashpower) {
        for (int key = 0; key < 10000; ++key) {
            check_key<string_int_table>(hashpower, std::to_string(key));
        }
    }
}

TEST_CASE("hash with larger hashpower only adds top bits",
          "[hash properties]") {
    std::string key = "abc";
    size_t hv = string_int_table::hasher()(key);
    for (size_t hashpower = 1; hashpower < 30; ++hashpower) {
        auto partial = unit_test_internals_view::partial_key<string_int_table>(hv);
        size_t index_bucket1 =
                unit_test_internals_view::index_hash<string_int_table>(hashpower, hv);
        size_t index_bucket2 =
                unit_test_internals_view::index_hash<string_int_table>(hashpower + 1, hv);
        CHECK((index_bucket2 & ~(1L << hashpower)) == index_bucket1);

        size_t alt_bucket1 = unit_test_internals_view::alt_index<string_int_table>(
                hashpower, partial, index_bucket1);
        size_t alt_bucket2 = unit_test_internals_view::alt_index<string_int_table>(
                hashpower, partial, index_bucket2);

        CHECK((alt_bucket2 & ~(1L << hashpower)) == alt_bucket1);
    }
}
