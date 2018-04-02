#include <array>

#include <catch.hpp>

#include "unit_test_util.hpp"
#include <concurrent_hash_map/concurrent_hash_map.hpp>

TEST_CASE("rehash empty table", "[resize]") {
    int_int_table table(1);
    REQUIRE(unit_test_internals_view::hashpower(table) == 0);

    table.get_unsynchronized_view().rehash(20);
    REQUIRE(unit_test_internals_view::hashpower(table) == 20);

    table.get_unsynchronized_view().rehash(1);
    REQUIRE(unit_test_internals_view::hashpower(table) == 1);
}

TEST_CASE("reserve calc", "[resize]") {
    const size_t slot_per_bucket = std::private_impl::DEFAULT_SLOTS_PER_BUCKET;
    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(0) == 0);
    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            1 * slot_per_bucket) == 0);

    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            2 * slot_per_bucket) == 1);
    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            3 * slot_per_bucket) == 2);
    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            4 * slot_per_bucket) == 2);
    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            2500000 * slot_per_bucket) == 22);

    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            (1UL << 31) * slot_per_bucket) == 31);
    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            ((1UL << 31) + 1) * slot_per_bucket) == 32);

    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            (1UL << 61) * slot_per_bucket) == 61);
    REQUIRE(unit_test_internals_view::reserve_calc<int_int_table>(
            ((1ULL << 61) + 1) * slot_per_bucket) == 62);
}

struct my_type {
    int x;

    ~my_type() { ++num_deletes; }

    static size_t num_deletes;
};

size_t my_type::num_deletes = 0;

TEST_CASE("Resizing number of frees", "[resize]") {
    my_type val{0};
    size_t num_deletes_after_resize;
    {
        // Should allocate 2 buckets of 4 slots
        std::concurrent_unordered_map<int, my_type, std::hash<int>, std::equal_to<int>,
                std::allocator<std::pair<const int, my_type>>>
                map(8);
        for (int i = 0; i < 9; ++i) {
            map.emplace(i, val);
        }
        // All of the items should be moved during resize to the new region of
        // memory. Then up to 8 of them can be moved to their new bucket.
        REQUIRE(my_type::num_deletes >= 8);
        REQUIRE(my_type::num_deletes <= 16);
        num_deletes_after_resize = my_type::num_deletes;
    }
    REQUIRE(my_type::num_deletes == num_deletes_after_resize + 9);
}

// Taken from https://github.com/facebook/folly/blob/master/folly/docs/Traits.md
class non_relocatable_type {
public:
    std::array<char, 1024> buffer;
    char *pointerToBuffer;

    non_relocatable_type() : pointerToBuffer(buffer.data()) {}

    non_relocatable_type(char c) : pointerToBuffer(buffer.data()) {
        buffer.fill(c);
    }

    non_relocatable_type(const non_relocatable_type &x) noexcept
            : buffer(x.buffer), pointerToBuffer(buffer.data()) {}

    non_relocatable_type &operator=(const non_relocatable_type &x) {
        buffer = x.buffer;
        return *this;
    }
};

TEST_CASE("Resize on non-relocatable type", "[resize]") {
    std::concurrent_unordered_map<int, non_relocatable_type, std::hash<int>, std::equal_to<int>,
            std::allocator<std::pair<const int, non_relocatable_type>>>
            map(0);
    REQUIRE(unit_test_internals_view::hashpower(map) == 0);
    // Make it resize a few times to ensure the vector capacity has to actually
    // change when we resize the buckets
    const size_t num_elems = 16;
    for (int i = 0; i < num_elems; ++i) {
        map.emplace(i, 'a');
    }
    // Make sure each pointer actually points to its buffer
    non_relocatable_type value;
    std::array<char, 1024> ref;
    ref.fill('a');
    auto lt = map.get_unsynchronized_view();
    for (const auto &kvpair : lt) {
        REQUIRE(ref == kvpair.second.buffer);
        REQUIRE(kvpair.second.pointerToBuffer == kvpair.second.buffer.data());
    }
}
