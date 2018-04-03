#include <catch.hpp>

#include <string>
#include <utility>

#include "unit_test_util.hpp"

using tbl = unique_ptr_table<int>;
using uptr = std::unique_ptr<int>;

const size_t TBL_INIT = 1;
const size_t TBL_SIZE = TBL_INIT * std::private_impl::DEFAULT_SLOTS_PER_BUCKET * 2;

void check_key_eq(tbl &tbl, int key, int expected_val) {
    size_t count = 0;
    tbl.visit(std::move(uptr(new int(key))), [expected_val, &count](const uptr &ptr) {
        ++count;
        REQUIRE(*ptr == expected_val);
    });
    REQUIRE(count > 0);
}

TEST_CASE("noncopyable insert and update", "[noncopyable]") {
    tbl tbl(TBL_INIT);
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        REQUIRE(tbl.emplace(std::move(uptr(new int(i))), std::move(uptr(new int(i)))));
    }
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        check_key_eq(tbl, i, i);
    }
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        tbl.update(uptr(new int(i)), uptr(new int(i + 1)));
    }
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        check_key_eq(tbl, i, i + 1);
    }
}

TEST_CASE("noncopyable upsert", "[noncopyable]") {
    tbl tbl(TBL_INIT);
    auto increment = [](uptr &ptr) { *ptr += 1; };
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        tbl.emplace_or_visit(uptr(new int(i)), increment, uptr(new int(i)));
    }
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        check_key_eq(tbl, i, i);
    }
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        tbl.emplace_or_visit(uptr(new int(i)), increment, uptr(new int(i)));
    }
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        check_key_eq(tbl, i, i + 1);
    }
}

TEST_CASE("noncopyable iteration", "[noncopyable]") {
    tbl tbl(TBL_INIT);
    for (size_t i = 0; i < TBL_SIZE; ++i) {
        tbl.emplace(uptr(new int(i)), uptr(new int(i)));
    }
    {
        auto locked_tbl = tbl.make_unordered_map_view();
        for (auto &kv : locked_tbl) {
            REQUIRE(*kv.first == *kv.second);
            *kv.second += 1;
        }
    }
    {
        auto locked_tbl = tbl.make_unordered_map_view();
        for (auto &kv : locked_tbl) {
            REQUIRE(*kv.first == *kv.second - 1);
        }
    }
}

TEST_CASE("nested table", "[noncopyable]") {
    typedef std::concurrent_unordered_map<char, std::string> inner_tbl;
    typedef std::concurrent_unordered_map<std::string, std::unique_ptr<inner_tbl>> nested_tbl;
    nested_tbl tbl;
    std::string keys[] = {"abc", "def"};
    for (std::string &k : keys) {
        tbl.insert(std::make_pair(std::string(k), nested_tbl::mapped_type(new inner_tbl)));
        tbl.emplace_or_visit(k, [&k](nested_tbl::mapped_type &t) {
            for (char c : k) {
                t->insert(std::make_pair(c, std::string(k)));
            }
        });
    }
    for (std::string &k : keys) {
        REQUIRE(tbl.visit(k, [](nested_tbl::mapped_type &) {}));
        tbl.visit(k, [&k](nested_tbl::mapped_type &t) {
            for (char c : k) {
                REQUIRE(t->find(c) == k);
            }
        });
    }
}

TEST_CASE("noncopyable insert lifetime") {
    tbl tbl;

    // Successful insert
    SECTION("Successful insert") {
        uptr key(new int(20));
        uptr value(new int(20));
        REQUIRE(tbl.emplace(std::move(key), std::move(value)));
        REQUIRE(!static_cast<bool>(key));
        REQUIRE(!static_cast<bool>(value));
    }

    // Unsuccessful insert
    SECTION("Unsuccessful insert") {
        tbl.emplace(uptr(new int(20)), uptr(new int(20)));
        uptr key(new int(20));
        uptr value(new int(30));
        REQUIRE_FALSE(tbl.emplace(std::move(key), std::move(value)));
        REQUIRE(static_cast<bool>(key));
        REQUIRE(static_cast<bool>(value));
    }
}

TEST_CASE("noncopyable erase_fn") {
    tbl tbl;
    tbl.emplace(uptr(new int(10)), uptr(new int(10)));
    auto decrement_and_erase = [](uptr &p) {
        --(*p);
        return *p == 0;
    };
    uptr k(new int(10));
    for (int i = 0; i < 9; ++i) {
        tbl.erase(k, decrement_and_erase);
        REQUIRE(tbl.visit(k, [](tbl::mapped_type &) {}));
    }
    tbl.erase(k, decrement_and_erase);
    REQUIRE_FALSE(tbl.visit(k, [](tbl::mapped_type &) {}));
}
