#include <catch.hpp>

#include <array>
#include <cmath>
#include <stdexcept>

#include "unit_test_util.hpp"
#include <concurrent_hash_map/concurrent_hash_map.hpp>

using int_int_table = std::concurrent_unordered_map<int, int>;

TEST_CASE("default size", "[constructor]") {
    int_int_table t;
    const auto &tbl = t.get_unsynchronized_view();
    REQUIRE(tbl.size() == 0);
    REQUIRE(tbl.empty());
    REQUIRE(tbl.bucket_count() == 1UL << unit_test_internals_view::hashpower(t));
    REQUIRE(tbl.load_factor() == 0);
}

TEST_CASE("given size", "[constructor]") {
    int_int_table t(1);
    const auto &tbl = t.get_unsynchronized_view();

    REQUIRE(tbl.size() == 0);
    REQUIRE(tbl.empty());
    REQUIRE(tbl.bucket_count() == 1);
    REQUIRE(tbl.load_factor() == 0);
}

TEST_CASE("frees even with exceptions", "[constructor]") {
    using no_space_table = std::concurrent_unordered_map<int, int, std::hash<int>,
            std::hash<int>,
            tracking_allocator<int, 0>>;
    // Should throw when allocating anything
    REQUIRE_THROWS_AS(no_space_table(1), std::bad_alloc);
    REQUIRE(get_unfreed_bytes() == 0);

    typedef int_int_table_with_allocator<
            tracking_allocator<int, unit_test_internals_view::IntIntBucketSize * 2>>
            some_space_table;
    // Should throw when allocating things after the bucket
    REQUIRE_THROWS_AS(some_space_table(1), std::bad_alloc);
    REQUIRE(get_unfreed_bytes() == 0);
}

struct StatefulHash {
    StatefulHash(int state_) : state(state_) {}

    size_t operator()(int x) const { return x; }

    int state;
};

struct StatefulKeyEqual {
    StatefulKeyEqual(int state_) : state(state_) {}

    bool operator()(int x, int y) const { return x == y; }

    int state;
};

template<typename T>
struct StatefulAllocator {
    using value_type = T;
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T &;
    using const_reference = const T &;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    template<typename U>
    struct rebind {
        using other = StatefulAllocator<U>;
    };

    StatefulAllocator() : state(0) {}

    StatefulAllocator(int state_) : state(state_) {}

    template<typename U>
    StatefulAllocator(const StatefulAllocator<U> &other) : state(other.state) {}

    T *allocate(size_t n) { return std::allocator<T>().allocate(n); }

    void deallocate(T *p, size_t n) { std::allocator<T>().deallocate(p, n); }

    template<typename U, class... Args>
    void construct(U *p, Args &&... args) {
        new((void *) p) U(std::forward<Args>(args)...);
    }

    template<typename U>
    void destroy(U *p) { p->~U(); }

    StatefulAllocator select_on_container_copy_construction() const {
        return StatefulAllocator();
    }

    using propagate_on_container_swap = std::integral_constant<bool, true>;

    int state;
};

template<typename T, typename U>
bool operator==(const StatefulAllocator<T> &a1,
                const StatefulAllocator<U> &a2) {
    return a1.state == a2.state;
}

template<typename T, typename U>
bool operator!=(const StatefulAllocator<T> &a1,
                const StatefulAllocator<U> &a2) {
    return a1.state != a2.state;
}

using alloc_t = StatefulAllocator<std::pair<const int, int>>;
using tbl_t =
std::concurrent_unordered_map<int, int, StatefulHash, StatefulKeyEqual, alloc_t>;

TEST_CASE("stateful components", "[constructor]") {
    tbl_t map(8, StatefulHash(10), StatefulKeyEqual(20), alloc_t(30));
    REQUIRE(map.hash_function().state == 10);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(map.hash_function()(i) == i);
    }
    REQUIRE(map.key_eq().state == 20);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(map.key_eq()(i, i));
        REQUIRE_FALSE(map.key_eq()(i, i + 1));
    }
    REQUIRE(map.get_allocator().state == 30);
}

TEST_CASE("range constructor", "[constructor]") {
    std::array<typename alloc_t::value_type, 3> elems{{{1, 2}, {3, 4}, {5, 6}}};
    tbl_t map(elems.begin(), elems.end(), 3, StatefulHash(10),
              StatefulKeyEqual(20), alloc_t(30));
    REQUIRE(map.hash_function().state == 10);
    REQUIRE(map.key_eq().state == 20);
    REQUIRE(map.get_allocator().state == 30);
    for (int i = 1; i <= 5; i += 2) {
        REQUIRE(map.find(i).value() == i + 1);
    }
}

TEST_CASE("move constructor", "[constructor]") {
    tbl_t map(10, StatefulHash(10), StatefulKeyEqual(20), alloc_t(30));
    map.insert(std::make_pair(10, 10));
    tbl_t map2(std::move(map));
    const auto &m1 = map.get_unsynchronized_view();
    const auto &m2 = map2.get_unsynchronized_view();
    REQUIRE(m1.size() == 0);
    REQUIRE(m2.size() == 1);
    REQUIRE(m2.hash_function().state == 10);
    REQUIRE(m2.key_eq().state == 20);
    REQUIRE(m2.get_allocator().state == 30);
}

TEST_CASE("initializer list constructor", "[constructor]") {
    tbl_t map({{1, 2},
               {3, 4},
               {5, 6}}, 3, StatefulHash(10), StatefulKeyEqual(20),
              alloc_t(30));
    const auto &m = map.get_unsynchronized_view();
    REQUIRE(m.hash_function().state == 10);
    REQUIRE(m.key_eq().state == 20);
    REQUIRE(m.get_allocator().state == 30);
    for (int i = 1; i <= 5; i += 2) {
        REQUIRE(m.find(i)->second == i + 1);
    }
}

TEST_CASE("swap maps", "[constructor]") {
    tbl_t map({{1, 2}}, 1, StatefulHash(10), StatefulKeyEqual(20), alloc_t(30));
    tbl_t map2({{3, 4}}, 1, StatefulHash(40), StatefulKeyEqual(50), alloc_t(60));
    map.swap(map2);

    {
        const auto &m1 = map.get_unsynchronized_view();
        const auto &m2 = map2.get_unsynchronized_view();

        REQUIRE(m1.size() == 1);
        REQUIRE(m1.hash_function().state == 40);
        REQUIRE(m1.key_eq().state == 50);
        REQUIRE(m1.get_allocator().state == 30);

        REQUIRE(m2.size() == 1);
        REQUIRE(m2.hash_function().state == 10);
        REQUIRE(m2.key_eq().state == 20);
        REQUIRE(m2.get_allocator().state == 60);
    }
    std::swap(map, map2);

    {
        const auto &m1 = map.get_unsynchronized_view();
        const auto &m2 = map2.get_unsynchronized_view();

        REQUIRE(m1.size() == 1);
        REQUIRE(m1.hash_function().state == 10);
        REQUIRE(m1.key_eq().state == 20);
        REQUIRE(m1.get_allocator().state == 60);

        REQUIRE(m2.size() == 1);
        REQUIRE(m2.hash_function().state == 40);
        REQUIRE(m2.key_eq().state == 50);
        REQUIRE(m2.get_allocator().state == 30);
    }
}

TEST_CASE("move assign different allocators", "[constructor]") {
    tbl_t map({{1, 2}}, 1, StatefulHash(10), StatefulKeyEqual(20), alloc_t(30));
    tbl_t map2({{3, 4}}, 1, StatefulHash(40), StatefulKeyEqual(50), alloc_t(60));

    const auto &m1 = map.get_unsynchronized_view();
    const auto &m2 = map2.get_unsynchronized_view();

    map = std::move(map2);
    REQUIRE(m1.size() == 1);
    REQUIRE(m1.find(3)->second == 4);
    REQUIRE(m1.hash_function().state == 40);
    REQUIRE(m1.key_eq().state == 50);
    REQUIRE(m1.get_allocator().state == 60);

    REQUIRE(m2.hash_function().state == 40);
    REQUIRE(m2.key_eq().state == 50);
    REQUIRE(m2.get_allocator().state == 60);
}

TEST_CASE("move assign same allocators", "[constructor]") {
    tbl_t map({{1, 2}}, 1, StatefulHash(10), StatefulKeyEqual(20), alloc_t(30));
    tbl_t map2({{3, 4}}, 1, StatefulHash(40), StatefulKeyEqual(50), alloc_t(30));

    const auto &m1 = map.get_unsynchronized_view();
    const auto &m2 = map2.get_unsynchronized_view();

    map = std::move(map2);
    REQUIRE(m1.size() == 1);
    REQUIRE(m1.find(3)->second == 4);
    REQUIRE(m1.hash_function().state == 40);
    REQUIRE(m1.key_eq().state == 50);
    REQUIRE(m1.get_allocator().state == 30);

    REQUIRE(m2.size() == 0);
    REQUIRE(m2.hash_function().state == 40);
    REQUIRE(m2.key_eq().state == 50);
    REQUIRE(m2.get_allocator().state == 30);
}

TEST_CASE("initializer list assignment", "[constructor]") {
    tbl_t map({{1, 2}}, 1, StatefulHash(10), StatefulKeyEqual(20), alloc_t(30));
    {
        const auto &m = map.get_unsynchronized_view();
        REQUIRE(m.find(1)->second == 2);
    }
    map = {{3, 4}};
    {
        const auto &m = map.get_unsynchronized_view();
        REQUIRE(m.find(3)->second == 4);
    }
}
