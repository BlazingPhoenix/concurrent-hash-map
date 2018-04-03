#include <catch.hpp>

#include <stdexcept>

#include "unit_test_util.hpp"

namespace test_user_exceptions {
    void maybe_throw(bool throw_exception) {
        if (throw_exception) {
            throw std::runtime_error("exception");
        }
    }

    bool constructor_throw, move_throw, hash_throw, equality_throw;

    class exception_int {
    public:
        exception_int() {
            maybe_throw(constructor_throw);
            val = 0;
        }

        exception_int(size_t x) {
            maybe_throw(constructor_throw);
            val = x;
        }

        exception_int(const exception_int &i) {
            maybe_throw(constructor_throw);
            val = static_cast<size_t>(i);
        }

        exception_int(exception_int &&i) {
            maybe_throw(constructor_throw || move_throw);
            val = static_cast<size_t>(i);
        }

        exception_int &operator=(const exception_int &i) {
            maybe_throw(constructor_throw);
            val = static_cast<size_t>(i);
            return *this;
        }

        exception_int &operator=(exception_int &&i) {
            maybe_throw(constructor_throw || move_throw);
            val = static_cast<size_t>(i);
            return *this;
        }

        operator size_t() const { return val; }

    private:
        size_t val;
    };
}

namespace std {
    template<>
    struct hash<test_user_exceptions::exception_int> {
        size_t operator()(const test_user_exceptions::exception_int &x) const {
            test_user_exceptions::maybe_throw(test_user_exceptions::hash_throw);
            return x;
        }
    };

    template<>
    struct equal_to<test_user_exceptions::exception_int> {
        bool operator()(const test_user_exceptions::exception_int &lhs,
                        const test_user_exceptions::exception_int &rhs) const {
            test_user_exceptions::maybe_throw(test_user_exceptions::equality_throw);
            return static_cast<size_t>(lhs) == static_cast<size_t>(rhs);
        }
    };
}

typedef std::concurrent_unordered_map<test_user_exceptions::exception_int, size_t, std::hash<test_user_exceptions::exception_int>,
        std::equal_to<test_user_exceptions::exception_int>>
        exception_table;

void check_iter_table(exception_table &tbl, size_t expectedSize) {
    auto lockedTable = tbl.make_unordered_map_view();
    size_t actual_size = 0;
    for (auto it = lockedTable.begin(); it != lockedTable.end(); ++it) {
        ++actual_size;
    }
    REQUIRE(actual_size == expectedSize);
}

TEST_CASE("user exceptions", "[user_exceptions]") {
    test_user_exceptions::constructor_throw = test_user_exceptions::hash_throw = test_user_exceptions::equality_throw =
    test_user_exceptions::move_throw = false;
    // We don't use sub-sections because CATCH is not exactly thread-safe

    // "find/contains"
    {
        exception_table tbl;
        tbl.insert(std::make_pair(1, 1));
        tbl.insert(std::make_pair(2, 2));
        tbl.insert(std::make_pair(3, 3));
        test_user_exceptions::hash_throw = true;
        REQUIRE_THROWS_AS(tbl.find(3), std::runtime_error);
        test_user_exceptions::hash_throw = false;
        test_user_exceptions::equality_throw = true;
        REQUIRE_THROWS_AS(tbl.find(3), std::runtime_error);
        test_user_exceptions::equality_throw = false;
        REQUIRE(tbl.find(3) == std::experimental::make_optional(3UL));
        check_iter_table(tbl, 3);
    }

    // "insert"
    {
        exception_table tbl;
        test_user_exceptions::constructor_throw = true;
        REQUIRE_THROWS_AS(tbl.insert(std::make_pair(100, 100)), std::runtime_error);
        test_user_exceptions::constructor_throw = false;
        REQUIRE(tbl.insert(std::make_pair(100, 100)));
        check_iter_table(tbl, 1);
    }

    // "erase"
    {
        exception_table tbl;
        for (int i = 0; i < 10; ++i) {
            tbl.insert(std::make_pair(i, i));
        }
        test_user_exceptions::hash_throw = true;
        REQUIRE_THROWS_AS(tbl.erase(5), std::runtime_error);
        test_user_exceptions::hash_throw = false;
        test_user_exceptions::equality_throw = true;
        REQUIRE_THROWS_AS(tbl.erase(5), std::runtime_error);
        test_user_exceptions::equality_throw = false;
        REQUIRE(tbl.erase(5));
        check_iter_table(tbl, 9);
    }

    // "update"
    {
        exception_table tbl;
        tbl.insert(std::make_pair(9, 9));
        tbl.insert(std::make_pair(10, 10));
        test_user_exceptions::hash_throw = true;
        REQUIRE_THROWS_AS(tbl.update(9, 10), std::runtime_error);
        test_user_exceptions::hash_throw = false;
        test_user_exceptions::equality_throw = true;
        REQUIRE_THROWS_AS(tbl.update(9, 10), std::runtime_error);
        test_user_exceptions::equality_throw = false;
        REQUIRE(tbl.update(9, 10));
        check_iter_table(tbl, 2);
    }

    // "update_fn"
    {
        exception_table tbl;
        tbl.insert(std::make_pair(9, 9));
        tbl.insert(std::make_pair(10, 10));
        auto updater = [](size_t &val) { val++; };
        test_user_exceptions::hash_throw = true;
        REQUIRE_THROWS_AS(tbl.visit(9, updater), std::runtime_error);
        test_user_exceptions::hash_throw = false;
        test_user_exceptions::equality_throw = true;
        REQUIRE_THROWS_AS(tbl.visit(9, updater), std::runtime_error);
        test_user_exceptions::equality_throw = false;
        REQUIRE(tbl.visit(9, updater));
        check_iter_table(tbl, 2);
    }
}
