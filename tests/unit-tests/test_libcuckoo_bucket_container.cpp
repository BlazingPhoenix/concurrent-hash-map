#include <catch.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "unit_test_util.hpp"
#include <iostream>

template<bool PROPAGATE_COPY_ASSIGNMENT = true,
        bool PROPAGATE_MOVE_ASSIGNMENT = true, bool PROPAGATE_SWAP = true>
struct allocator_wrapper {
    template<class T>
    class stateful_allocator {
    public:
        using value_type = T;
        using propagate_on_container_copy_assignment =
        std::integral_constant<bool, PROPAGATE_COPY_ASSIGNMENT>;
        using propagate_on_container_move_assignment =
        std::integral_constant<bool, PROPAGATE_MOVE_ASSIGNMENT>;
        using propagate_on_container_swap =
        std::integral_constant<bool, PROPAGATE_SWAP>;

        stateful_allocator() : id(0) {}

        stateful_allocator(const size_t &id_) : id(id_) {}

        stateful_allocator(const stateful_allocator &other) : id(other.id) {}

        template<class U>
        stateful_allocator(const stateful_allocator<U> &other) : id(other.id) {}

        stateful_allocator &operator=(const stateful_allocator &a) {
            id = a.id + 1;
            return *this;
        }

        stateful_allocator &operator=(stateful_allocator &&a) {
            id = a.id + 2;
            return *this;
        }

        T *allocate(size_t n) { return std::allocator<T>().allocate(n); }

        void deallocate(T *ptr, size_t n) {
            std::allocator<T>().deallocate(ptr, n);
        }

        stateful_allocator select_on_container_copy_construction() const {
            stateful_allocator copy(*this);
            ++copy.id;
            return copy;
        }

        bool operator==(const stateful_allocator &other) { return id == other.id; }

        bool operator!=(const stateful_allocator &other) { return id != other.id; }

        size_t id;
    };
};

template<class T, bool PCA = true, bool PMA = true>
using stateful_allocator =
typename allocator_wrapper<PCA, PMA>::template stateful_allocator<T>;

const size_t SLOT_PER_BUCKET = 4;

template<class Alloc>
using testing_container =
std::private_impl::bucket_container<std::shared_ptr<int>, int, Alloc, uint8_t,
        SLOT_PER_BUCKET>;

using value_type = std::pair<const std::shared_ptr<int>, int>;

TEST_CASE("bucket container default constructor", "[bucket container]") {
    allocator_wrapper<>::stateful_allocator<value_type> a;
    testing_container<decltype(a)> tc(2, a);
    REQUIRE(tc.hashpower() == 2);
    REQUIRE(tc.size() == 4);
    REQUIRE(tc.get_allocator().id == 0);
    for (size_t i = 0; i < tc.size(); ++i) {
        for (size_t j = 0; j < SLOT_PER_BUCKET; ++j) {
            REQUIRE_FALSE(tc[i].occupied(j));
        }
    }
}

TEST_CASE("bucket container simple stateful allocator", "[bucket container]") {
    allocator_wrapper<>::stateful_allocator<value_type> a(10);
    testing_container<decltype(a)> tc(2, a);
    REQUIRE(tc.hashpower() == 2);
    REQUIRE(tc.size() == 4);
    REQUIRE(tc.get_allocator().id == 10);
}

TEST_CASE("bucket container copy construction", "[bucket container]") {
    allocator_wrapper<>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    testing_container<decltype(a)> tc2(tc);

    REQUIRE(tc[0].occupied(0));
    REQUIRE(tc[0].partial(0) == 2);
    REQUIRE(*tc[0].key(0) == 10);
    REQUIRE(tc[0].mapped(0) == 5);
    REQUIRE(tc.get_allocator().id == 5);

    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE(tc2.get_allocator().id == 6);
}

TEST_CASE("bucket container move construction", "[bucket container]") {
    allocator_wrapper<>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    testing_container<decltype(a)> tc2(std::move(tc));

    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE(tc2.get_allocator().id == 5);
}

TEST_CASE("bucket container copy assignment with propagate",
          "[bucket container]") {
    allocator_wrapper<true>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    testing_container<decltype(a)> tc2(2, a);
    tc2.set_element(1, 0, 2, std::make_shared<int>(10), 5);

    tc2 = tc;
    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].key(0).use_count() == 2);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE_FALSE(tc2[1].occupied(0));

    REQUIRE(tc.get_allocator().id == 5);
    REQUIRE(tc2.get_allocator().id == 6);
}

TEST_CASE("bucket container copy assignment no propagate",
          "[bucket container]") {
    allocator_wrapper<false>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    testing_container<decltype(a)> tc2(2, a);
    tc2.set_element(1, 0, 2, std::make_shared<int>(10), 5);

    tc2 = tc;
    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].key(0).use_count() == 2);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE_FALSE(tc2[1].occupied(0));

    REQUIRE(tc.get_allocator().id == 5);
    REQUIRE(tc2.get_allocator().id == 5);
}

TEST_CASE("bucket container move assignment with propagate",
          "[bucket container]") {
    allocator_wrapper<>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    testing_container<decltype(a)> tc2(2, a);
    tc2.set_element(1, 0, 2, std::make_shared<int>(10), 5);

    tc2 = std::move(tc);
    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE_FALSE(tc2[1].occupied(0));
    REQUIRE(tc2.get_allocator().id == 7);
}

TEST_CASE("bucket container move assignment no propagate equal",
          "[bucket container]") {
    allocator_wrapper<true, false>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    testing_container<decltype(a)> tc2(2, a);
    tc2.set_element(1, 0, 2, std::make_shared<int>(10), 5);

    tc2 = std::move(tc);
    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].key(0).use_count() == 1);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE_FALSE(tc2[1].occupied(0));
    REQUIRE(tc2.get_allocator().id == 5);
}

TEST_CASE("bucket container move assignment no propagate unequal",
          "[bucket container]") {
    allocator_wrapper<true, false>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    allocator_wrapper<true, false>::stateful_allocator<value_type> a2(4);
    testing_container<decltype(a)> tc2(2, a2);
    tc2.set_element(1, 0, 2, std::make_shared<int>(10), 5);

    tc2 = std::move(tc);
    REQUIRE(!tc2[1].occupied(0));
    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].key(0).use_count() == 1);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE_FALSE(tc2[1].occupied(0));
    REQUIRE(tc2.get_allocator().id == 4);

    REQUIRE(tc[0].occupied(0));
    REQUIRE(tc[0].partial(0) == 2);
    REQUIRE_FALSE(tc[0].key(0));
}

TEST_CASE("bucket container swap no propagate", "[bucket container]") {
    allocator_wrapper<true, true, false>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    testing_container<decltype(a)> tc2(2, a);
    tc2.set_element(1, 0, 2, std::make_shared<int>(10), 5);

    tc.swap(tc2);

    REQUIRE(tc[1].occupied(0));
    REQUIRE(tc[1].partial(0) == 2);
    REQUIRE(*tc[1].key(0) == 10);
    REQUIRE(tc[1].key(0).use_count() == 1);
    REQUIRE(tc[1].mapped(0) == 5);
    REQUIRE(tc.get_allocator().id == 5);

    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].key(0).use_count() == 1);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE(tc2.get_allocator().id == 5);
}

TEST_CASE("bucket container swap propagate", "[bucket container]") {
    allocator_wrapper<true, true, true>::stateful_allocator<value_type> a(5);
    testing_container<decltype(a)> tc(2, a);
    tc.set_element(0, 0, 2, std::make_shared<int>(10), 5);
    testing_container<decltype(a)> tc2(2, a);
    tc2.set_element(1, 0, 2, std::make_shared<int>(10), 5);

    tc.swap(tc2);

    REQUIRE(tc[1].occupied(0));
    REQUIRE(tc[1].partial(0) == 2);
    REQUIRE(*tc[1].key(0) == 10);
    REQUIRE(tc[1].key(0).use_count() == 1);
    REQUIRE(tc[1].mapped(0) == 5);
    REQUIRE(tc.get_allocator().id == 7);

    REQUIRE(tc2[0].occupied(0));
    REQUIRE(tc2[0].partial(0) == 2);
    REQUIRE(*tc2[0].key(0) == 10);
    REQUIRE(tc2[0].key(0).use_count() == 1);
    REQUIRE(tc2[0].mapped(0) == 5);
    REQUIRE(tc2.get_allocator().id == 7);
}

struct exception_int {
    int x;
    static bool do_throw;

    exception_int(int x_) : x(x_) { maybe_throw(); }

    exception_int(const exception_int &other) : x(other.x) { maybe_throw(); }

    exception_int &operator=(const exception_int &other) {
        x = other.x;
        maybe_throw();
        return *this;
    }

    ~exception_int() { maybe_throw(); }

private:
    void maybe_throw() {
        if (do_throw) {
            throw std::runtime_error("thrown");
        }
    }
};

bool exception_int::do_throw = false;

using exception_container =
std::private_impl::bucket_container<exception_int, int,
        std::allocator<std::pair<exception_int, int>>,
        uint8_t, SLOT_PER_BUCKET>;

TEST_CASE("set_element with throwing type maintains strong guarantee",
          "[bucket container]") {
    exception_container container(0, exception_container::allocator_type());
    container.set_element(0, 0, 0, exception_int(10), 20);

    exception_int::do_throw = true;
    REQUIRE_THROWS_AS(container.set_element(0, 1, 0, 0, 0), std::runtime_error);
    exception_int::do_throw = false;

    REQUIRE(container[0].occupied(0));
    REQUIRE(container[0].key(0).x == 10);
    REQUIRE(container[0].mapped(0) == 20);

    REQUIRE_FALSE(container[0].occupied(1));
}

TEST_CASE("copy assignment with throwing type is destroyed properly",
          "[bucket container]") {
    exception_container container(0, exception_container::allocator_type());
    container.set_element(0, 0, 0, exception_int(10), 20);
    exception_container other(0, exception_container::allocator_type());

    exception_int::do_throw = true;
    REQUIRE_THROWS_AS(other = container, std::runtime_error);
    exception_int::do_throw = false;
}
