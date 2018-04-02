// Utilities for unit testing
#ifndef UNIT_TEST_UTIL_HH_
#define UNIT_TEST_UTIL_HH_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include <concurrent_hash_map/concurrent_hash_map.hpp>

// Returns a statically allocated value used to keep track of how many unfreed
// bytes have been allocated. This value is shared across all threads.
std::atomic<int64_t> &get_unfreed_bytes();

// We define a a allocator class that keeps track of how many unfreed bytes have
// been allocated. Users can specify an optional bound for how many bytes can be
// unfreed, and the allocator will fail if asked to allocate above that bound
// (note that behavior with this bound with concurrent allocations will be hard
// to deal with). A bound below 0 is inactive (the default is -1).
template<class T, int64_t BOUND = -1>
struct tracking_allocator {
    using value_type = T;
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T &;
    using const_reference = const T &;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    template<typename U>
    struct rebind {
        using other = tracking_allocator<U, BOUND>;
    };

    tracking_allocator() {}

    template<typename U>
    tracking_allocator(const tracking_allocator<U, BOUND> &) {}

    T *allocate(size_t n) {
        const size_t bytes_to_allocate = sizeof(T) * n;
        if (BOUND >= 0 && get_unfreed_bytes() + bytes_to_allocate > BOUND) {
            throw std::bad_alloc();
        }
        get_unfreed_bytes() += bytes_to_allocate;
        return std::allocator<T>().allocate(n);
    }

    void deallocate(T *p, size_t n) {
        get_unfreed_bytes() -= (sizeof(T) * n);
        std::allocator<T>().deallocate(p, n);
    }

    template<typename U, class... Args>
    void construct(U *p, Args &&... args) {
        new((void *) p) U(std::forward<Args>(args)...);
    }

    template<typename U>
    void destroy(U *p) { p->~U(); }
};

template<typename T, typename U, int64_t BOUND>
bool operator==(const tracking_allocator<T, BOUND> &a1,
                const tracking_allocator<U, BOUND> &a2) {
    return true;
}

template<typename T, typename U, int64_t BOUND>
bool operator!=(const tracking_allocator<T, BOUND> &a1,
                const tracking_allocator<U, BOUND> &a2) {
    return false;
}

using int_int_table =
std::concurrent_unordered_map<int, int, std::hash<int>, std::equal_to<int>,
        std::allocator<std::pair<const int, int>>>;

template<class Alloc>
using int_int_table_with_allocator =
std::concurrent_unordered_map<int, int, std::hash<int>, std::equal_to<int>, Alloc>;

using string_int_table =
std::concurrent_unordered_map<std::string, int, std::hash<std::string>,
        std::equal_to<std::string>,
        std::allocator<std::pair<const std::string, int>>>;

namespace std {
    template<typename T>
    struct hash<unique_ptr<T>> {
        size_t operator()(const unique_ptr<T> &ptr) const {
            return std::hash<T>()(*ptr);
        }

        size_t operator()(const T *ptr) const { return std::hash<T>()(*ptr); }
    };

    template<typename T>
    struct equal_to<unique_ptr<T>> {
        bool operator()(const unique_ptr<T> &ptr1, const unique_ptr<T> &ptr2) const {
            return *ptr1 == *ptr2;
        }

        bool operator()(const T *ptr1, const unique_ptr<T> &ptr2) const {
            return *ptr1 == *ptr2;
        }

        bool operator()(const unique_ptr<T> &ptr1, const T *ptr2) const {
            return *ptr1 == *ptr2;
        }
    };
}

template<typename T>
using unique_ptr_table = std::concurrent_unordered_map<
        std::unique_ptr<T>, std::unique_ptr<T>, std::hash<std::unique_ptr<T>>,
        std::equal_to<std::unique_ptr<T>>,
        std::allocator<std::pair<const std::unique_ptr<T>, std::unique_ptr<T>>>>;

// Some unit tests need access into certain private data members of the table.
// This class is a friend of the table, so it can access those.

class unit_test_internals_view {
public:
    static const size_t IntIntBucketSize = sizeof(
            std::private_impl::bucket_container<int, int, std::allocator<std::pair<const int, int>>,
                    std::private_impl::partial_t,
                    std::private_impl::DEFAULT_SLOTS_PER_BUCKET>::bucket);

    template<class concurrent_map>
    static typename concurrent_map::partial_t partial_key(const size_t hv) {
        return concurrent_map::partial_key(hv);
    }

    template<class concurrent_map>
    static size_t index_hash(const size_t hashpower, const size_t hv) {
        return concurrent_map::index_hash(hashpower, hv);
    }

    template<class concurrent_map>
    static size_t alt_index(const size_t hashpower,
                            const typename concurrent_map::partial_t partial,
                            const size_t index) {
        return concurrent_map::alt_index(hashpower, partial, index);
    }

    template<class concurrent_map>
    static size_t reserve_calc(size_t n) {
        return concurrent_map::reserve_calc(n);
    }

    template<class concurrent_map>
    static typename concurrent_map::locks_t &get_current_locks(const concurrent_map &table) {
        return table.locks;
    }

    template<class concurrent_map>
    static typename concurrent_map::size_type hashpower(const concurrent_map &table) {
        return table.hashpower();
    }
};

#endif // UNIT_TEST_UTIL_HH_
