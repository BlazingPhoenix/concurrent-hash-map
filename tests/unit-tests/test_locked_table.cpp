#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include <catch.hpp>

#include "unit_test_util.hpp"

TEST_CASE("locked_table typedefs", "[locked_table]") {
    using tbl = int_int_table;
    using ltbl = tbl::unordered_map_view;
    const bool key_type = std::is_same<tbl::key_type, ltbl::key_type>::value;
    const bool mapped_type =
            std::is_same<tbl::mapped_type, ltbl::mapped_type>::value;
    const bool value_type =
            std::is_same<tbl::value_type, ltbl::value_type>::value;
    const bool size_type = std::is_same<tbl::size_type, ltbl::size_type>::value;
    const bool hasher = std::is_same<tbl::hasher, ltbl::hasher>::value;
    const bool key_equal = std::is_same<tbl::key_equal, ltbl::key_equal>::value;
    const bool allocator_type =
            std::is_same<tbl::allocator_type, ltbl::allocator_type>::value;
    REQUIRE(key_type);
    REQUIRE(mapped_type);
    REQUIRE(value_type);
    REQUIRE(size_type);
    REQUIRE(hasher);
    REQUIRE(key_equal);
    REQUIRE(allocator_type);
}

TEST_CASE("locked_table move", "[locked_table]") {
    int_int_table tbl;

    SECTION("move constructor") {
        auto lt = tbl.make_unordered_map_view();
        auto lt2(std::move(lt));
//    REQUIRE(!lt.is_active());
//    REQUIRE(lt2.is_active());
    }

    SECTION("move assignment") {
        auto lt = tbl.make_unordered_map_view();
        auto lt2 = std::move(lt);
//    REQUIRE(!lt.is_active());
//    REQUIRE(lt2.is_active());
    }

    SECTION("iterators compare after table is moved") {
        auto lt1 = tbl.make_unordered_map_view();
        auto it1 = lt1.begin();
        auto it2 = lt1.begin();
        REQUIRE(it1 == it2);
        auto lt2(std::move(lt1));
        REQUIRE(it1 == it2);
    }
}

TEST_CASE("locked_table info", "[locked_table]") {
    int_int_table tbl;
    tbl.insert(std::make_pair(10, 10));
    auto lt = tbl.make_unordered_map_view();

    // We should still be able to call table info operations on the
    // cuckoohash_map instance, because they shouldn't take locks.

    REQUIRE(lt.get_allocator() == tbl.get_allocator());
    lt.rehash(5);
}

TEST_CASE("locked_table clear", "[locked_table]") {
    int_int_table tbl;
    tbl.insert(std::make_pair(10, 10));
    auto lt = tbl.make_unordered_map_view();
    REQUIRE(lt.size() == 1);
    lt.clear();
    REQUIRE(lt.size() == 0);
    lt.clear();
    REQUIRE(lt.size() == 0);
}

TEST_CASE("locked_table insert duplicate", "[locked_table]") {
    int_int_table tbl;
    tbl.insert(std::make_pair(10, 10));
    {
        auto lt = tbl.make_unordered_map_view();
        auto result = lt.insert(std::make_pair(10, 20));
        REQUIRE(result.first->first == 10);
        REQUIRE(result.first->second == 10);
        REQUIRE_FALSE(result.second);
        result.first->second = 50;
    }
    REQUIRE(*tbl.find(10) == 50);
}

TEST_CASE("locked_table insert new key", "[locked_table]") {
    int_int_table tbl;
    tbl.insert(std::make_pair(10, 10));
    {
        auto lt = tbl.make_unordered_map_view();
        auto result = lt.insert(std::make_pair(20, 20));
        REQUIRE(result.first->first == 20);
        REQUIRE(result.first->second == 20);
        REQUIRE(result.second);
        result.first->second = 50;
    }
    REQUIRE(*tbl.find(10) == 10);
    REQUIRE(*tbl.find(20) == 50);
}

TEST_CASE("locked_table insert lifetime", "[locked_table]") {
    unique_ptr_table<int> tbl;

    SECTION("Successful insert") {
        auto lt = tbl.make_unordered_map_view();
        std::unique_ptr<int> key(new int(20));
        std::unique_ptr<int> value(new int(20));


        auto result = lt.insert(std::move(std::make_pair(std::move(key), std::move(value))));
        REQUIRE(*result.first->first == 20);
        REQUIRE(*result.first->second == 20);
        REQUIRE(result.second);
        REQUIRE(!static_cast<bool>(key));
        REQUIRE(!static_cast<bool>(value));
    }

    SECTION("Unsuccessful insert") {
        tbl.emplace(std::unique_ptr<int>(new int(20)), std::unique_ptr<int>(new int(20)));
        auto lt = tbl.make_unordered_map_view();
        std::unique_ptr<int> key(new int(20));
        std::unique_ptr<int> value(new int(30));
        unique_ptr_table<int>::value_type val(std::move(key), std::move(value));
        auto result = lt.insert(std::move(val));
        REQUIRE(*result.first->first == 20);
        REQUIRE(*result.first->second == 20);
        REQUIRE(!result.second);
        REQUIRE(static_cast<bool>(val.first));
        REQUIRE(static_cast<bool>(val.second));
    }
}

TEST_CASE("locked_table erase", "[locked_table]") {
    int_int_table tbl;
    for (int i = 0; i < 5; ++i) {
        tbl.insert(std::make_pair(i, i));
    }
    using lt_t = int_int_table::unordered_map_view;

    SECTION("simple erase") {
        auto lt = tbl.make_unordered_map_view();
        lt_t::const_iterator const_it;
        const_it = lt.find(0);
        REQUIRE(const_it != lt.end());
        lt_t::const_iterator const_next = const_it;
        ++const_next;
        REQUIRE(static_cast<lt_t::const_iterator>(lt.erase(const_it)) ==
                const_next);
        REQUIRE(lt.size() == 4);

        lt_t::iterator it;
        it = lt.find(1);
        lt_t::iterator next = it;
        ++next;
        REQUIRE(lt.erase(static_cast<lt_t::const_iterator>(it)) == next);
        REQUIRE(lt.size() == 3);

        REQUIRE(lt.erase(2) == 1);
        REQUIRE(lt.size() == 2);
    }

    SECTION("erase doesn't ruin this iterator") {
        auto lt = tbl.make_unordered_map_view();
        auto it = lt.begin();
        auto next = it;
        ++next;
        REQUIRE(lt.erase(it) == next);
        ++it;
        REQUIRE(it->first >= 0);
        REQUIRE(it->first < 5);
        REQUIRE(it->second >= 0);
        REQUIRE(it->second < 5);
    }

    SECTION("erase doesn't ruin other iterators") {
        auto lt = tbl.make_unordered_map_view();
        auto it0 = lt.find(0);
        auto it1 = lt.find(1);
        auto it2 = lt.find(2);
        auto it3 = lt.find(3);
        auto it4 = lt.find(4);
        auto next = it2;
        ++next;
        REQUIRE(lt.erase(it2) == next);
        REQUIRE(it0->first == 0);
        REQUIRE(it0->second == 0);
        REQUIRE(it1->first == 1);
        REQUIRE(it1->second == 1);
        REQUIRE(it3->first == 3);
        REQUIRE(it3->second == 3);
        REQUIRE(it4->first == 4);
        REQUIRE(it4->second == 4);
    }
}

TEST_CASE("locked_table find", "[locked_table]") {
    int_int_table tbl;
    using lt_t = int_int_table::unordered_map_view;
    auto lt = tbl.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(lt.insert(std::make_pair(i, i)).second);
    }
    bool found_begin_elem = false;
    bool found_last_elem = false;
    for (int i = 0; i < 10; ++i) {
        lt_t::iterator it = lt.find(i);
        lt_t::const_iterator const_it = lt.find(i);
        REQUIRE(it != lt.end());
        REQUIRE(it->first == i);
        REQUIRE(it->second == i);
        REQUIRE(const_it != lt.end());
        REQUIRE(const_it->first == i);
        REQUIRE(const_it->second == i);
        it->second++;
        if (it == lt.begin()) {
            found_begin_elem = true;
        }
        if (++it == lt.end()) {
            found_last_elem = true;
        }
    }
    REQUIRE(found_begin_elem);
    REQUIRE(found_last_elem);
    for (int i = 0; i < 10; ++i) {
        lt_t::iterator it = lt.find(i);
        REQUIRE(it->first == i);
        REQUIRE(it->second == i + 1);
    }
}

TEST_CASE("locked_table at", "[locked_table]") {
    int_int_table tbl;
    auto lt = tbl.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(lt.insert(std::make_pair(i, i)).second);
    }
    for (int i = 0; i < 10; ++i) {
        int &val = lt.at(i);
        const int &const_val =
                const_cast<const int_int_table::unordered_map_view &>(lt).at(i);
        REQUIRE(val == i);
        REQUIRE(const_val == i);
        ++val;
    }
    for (int i = 0; i < 10; ++i) {
        REQUIRE(lt.at(i) == i + 1);
    }
    REQUIRE_THROWS_AS(lt.at(11), std::out_of_range);
}

TEST_CASE("locked_table operator[]", "[locked_table]") {
    int_int_table tbl;
    auto lt = tbl.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(lt.insert(std::make_pair(i, i)).second);
    }
    for (int i = 0; i < 10; ++i) {
        int &val = lt[i];
        REQUIRE(val == i);
        ++val;
    }
    for (int i = 0; i < 10; ++i) {
        REQUIRE(lt[i] == i + 1);
    }
    REQUIRE(lt[11] == 0);
    REQUIRE(lt.at(11) == 0);
}

TEST_CASE("locked_table count", "[locked_table]") {
    int_int_table tbl;
    auto lt = tbl.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(lt.insert(std::make_pair(i, i)).second);
    }
    for (int i = 0; i < 10; ++i) {
        REQUIRE(lt.count(i) == 1);
    }
    REQUIRE(lt.count(11) == 0);
}

TEST_CASE("locked_table equal_range", "[locked_table]") {
    int_int_table tbl;
    using lt_t = int_int_table::unordered_map_view;
    auto lt = tbl.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(lt.insert(std::make_pair(i, i)).second);
    }
    for (int i = 0; i < 10; ++i) {
        std::pair<lt_t::iterator, lt_t::iterator> it_range = lt.equal_range(i);
        REQUIRE(it_range.first->first == i);
        REQUIRE(++it_range.first == it_range.second);
        std::pair<lt_t::const_iterator, lt_t::const_iterator> const_it_range =
                lt.equal_range(i);
        REQUIRE(const_it_range.first->first == i);
        REQUIRE(++const_it_range.first == const_it_range.second);
    }
    auto it_range = lt.equal_range(11);
    REQUIRE(it_range.first == lt.end());
    REQUIRE(it_range.second == lt.end());
}

TEST_CASE("locked_table equality", "[locked_table]") {
    int_int_table tbl1(40);
    auto lt1 = tbl1.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        lt1.insert(std::make_pair(i, i));
    }

    int_int_table tbl2(30);
    auto lt2 = tbl2.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        lt2.insert(std::make_pair(i, i));
    }

    int_int_table tbl3(30);
    auto lt3 = tbl3.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        lt3.insert(std::make_pair(i, i + 1));
    }

    int_int_table tbl4(40);
    auto lt4 = tbl4.make_unordered_map_view();
    for (int i = 0; i < 10; ++i) {
        lt4.insert(std::make_pair(i + 1, i));
    }

    REQUIRE(lt1 == lt2);
    REQUIRE_FALSE(lt2 != lt1);

    REQUIRE(lt1 != lt3);
    REQUIRE_FALSE(lt3 == lt1);
    REQUIRE_FALSE(lt2 == lt3);
    REQUIRE(lt3 != lt2);

    REQUIRE(lt1 != lt4);
    REQUIRE(lt4 != lt1);
    REQUIRE_FALSE(lt3 == lt4);
    REQUIRE_FALSE(lt4 == lt3);
}

template<typename Table>
void check_all_locks_taken(Table &tbl) {
    auto &locks = unit_test_internals_view::get_current_locks(tbl);
    for (size_t i = 0; i < locks.size(); ++i) {
        REQUIRE_FALSE(locks[i].try_lock(std::private_impl::LOCKING_ACTIVE()));
    }
}

TEST_CASE("locked table holds locks after resize", "[locked table]") {
    int_int_table tbl(4);
    auto lt = tbl.make_unordered_map_view();
    //TODO: FIXME add a method with locked view
    //  check_all_locks_taken(tbl);

    // After a cuckoo_fast_double, all locks are still taken
    for (int i = 0; i < 5; ++i) {
        lt.insert(std::make_pair(i, i));
    }
    //check_all_locks_taken(tbl);

    // After a cuckoo_simple_expand, all locks are still taken
    lt.rehash(10);
    //check_all_locks_taken(tbl);
}
