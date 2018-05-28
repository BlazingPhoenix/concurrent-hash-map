#pragma once

#include <utility>
#include <functional>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <array>
#include <mutex>
#include <string>
#include <thread>
#include <list>
#include <vector>
#include <experimental/optional>

class unit_test_internals_view;

namespace std {
    namespace private_impl {
        static constexpr const std::size_t DEFAULT_SLOTS_PER_BUCKET = 4;
        static constexpr const std::size_t DEFAULT_SIZE = (1U << 16) * DEFAULT_SLOTS_PER_BUCKET;
        static constexpr const std::size_t LOCK_ARRAY_GRANULARITY = 0;
        static constexpr const double DEFAULT_MINIMUM_LOAD_FACTOR = 0.05;
        static constexpr const std::size_t NO_MAXIMUM_HASHPOWER = std::numeric_limits<size_t>::max();
        static constexpr const std::size_t MAX_NUM_LOCKS = 1UL << 16;


        using size_type = std::size_t;
        using partial_t = uint8_t;

        template <typename Allocator>
        void copy_allocator(Allocator& dst, const Allocator& src, std::true_type) {
            dst = src;
        }

        template <typename Allocator>
        void copy_allocator(Allocator& dst, const Allocator& src, std::false_type) {
        }

        template <typename Allocator>
        void swap_allocator(Allocator& dst, Allocator& src, std::true_type) {
            std::swap(dst, src);
        }

        template <typename Allocator>
        void swap_allocator(Allocator& dst, Allocator& src, std::false_type) {
        }

        template <class Key, class Value, class Allocator, class PartialKey,
                  std::size_t SLOTS_PER_BUCKET>
        class bucket_container {
            public:
            using key_type = Key;
            using mapped_type = Value;
            using value_type = std::pair<const Key, Value>;

        private:
            using traits =
                typename std::allocator_traits<Allocator>::template rebind_traits<value_type>;
            using storage_value_type = std::pair<Key, Value>;

        public:
            using allocator_type = typename traits::allocator_type;
            using partial_t = PartialKey;
            using size_type = private_impl::size_type;
            using reference = value_type&;
            using const_reference = const value_type&;
            using pointer = value_type*;
            using const_pointer = const value_type*;

            class bucket {
            public:
                bucket() noexcept : occupied_flags{} {}

                const value_type& element(size_type index) const {
                    return *reinterpret_cast<const value_type *>(&values[index]);
                }
                value_type& element(size_type index) {
                    return *reinterpret_cast<value_type *>(&values[index]);
                }
                storage_value_type& storage_element(size_type index) {
                    return *reinterpret_cast<storage_value_type *>(&values[index]);
                }

                const key_type& key(size_type index) const {
                    return element(index).first;
                }
                key_type&& movable_key(size_type index) {
                    return std::move(const_cast<key_type&>(element(index).first));
                }

                const mapped_type& mapped(size_type index) const {
                    return element(index).second;
                }
                mapped_type& mapped(size_type index) {
                    return element(index).second;
                }

                partial_t partial(size_type index) const {
                    return partials[index];
                }
                partial_t& partial(size_type index) {
                    return partials[index];
                }

                bool occupied(size_type index) const {
                    return occupied_flags[index];
                }
                bool& occupied(size_type index) {
                    return occupied_flags[index];
                }

            private:
                std::array<typename std::aligned_storage<sizeof(storage_value_type),
                           alignof(storage_value_type)>::type, SLOTS_PER_BUCKET> values;
                std::array<partial_t, SLOTS_PER_BUCKET> partials;
                std::array<bool, SLOTS_PER_BUCKET> occupied_flags;
            };

            bucket_container(size_type hashpower, const allocator_type& allocator)
                : allocator(allocator),
                  bucket_allocator(allocator),
                  hashpower_holder(hashpower),
                  buckets(bucket_allocator.allocate(size())) {
                static_assert(std::is_nothrow_constructible<bucket>::value,
                              "bucket_container requires bucket to be nothrow "
                              "constructible");
                for (size_type i = 0; i < size(); ++i) {
                    traits::construct(this->allocator, &buckets[i]);
                }
            }

            ~bucket_container() noexcept { destroy_buckets(); }

            bucket_container(const bucket_container& other)
                : allocator(traits::select_on_container_copy_construction(other.allocator)),
                  bucket_allocator(allocator),
                  hashpower_holder(other.hashpower()),
                  buckets(transfer(other.hashpower(), other, std::false_type())) {}

            bucket_container(const bucket_container& other,
                             const allocator_type& allocator)
                : allocator(allocator),
                  bucket_allocator(allocator),
                  hashpower_holder(other.hashpower()),
                  buckets(transfer(other.hashpower(), other, std::false_type())) {}

            bucket_container(bucket_container&& other)
                : allocator(std::move(other.allocator))
                , bucket_allocator(allocator)
                , hashpower_holder(other.hashpower())
                , buckets(std::move(other.buckets)) {
                other.buckets = nullptr;
            }

            bucket_container(bucket_container&& other,
                             const allocator_type& allocator)
                : allocator(allocator)
                , bucket_allocator(allocator) {
                move_assign(other, std::false_type());
            }

            bucket_container& operator=(const bucket_container& other) {
                destroy_buckets();
                copy_allocator(allocator, other.allocator,
                               typename traits::propagate_on_container_copy_assignment());
                bucket_allocator = allocator;
                hashpower(other.hashpower());
                buckets = transfer(other.hashpower(), other, std::false_type());
                return *this;
            }

            bucket_container& operator=(bucket_container&& other) {
                destroy_buckets();
                move_assign(other, typename traits::propagate_on_container_move_assignment());
                return *this;
            }

            void swap(bucket_container& other) noexcept {
                swap_allocator(allocator, other.allocator,
                               typename traits::propagate_on_container_swap());
                bucket_allocator = allocator;
                // Regardless of whether we actually swapped the allocators or not, it will
                // always be okay to do the remainder of the swap. This is because if the
                // allocators were swapped, then the subsequent operations are okay. If the
                // allocators weren't swapped but compare equal, then we're okay. If they
                // weren't swapped and compare unequal, then behavior is undefined, so
                // we're okay.
                size_t other_hashpower = other.hashpower();
                other.hashpower(hashpower());
                hashpower(other_hashpower);
                std::swap(buckets, other.buckets);
            }

            size_type hashpower() const {
                return hashpower_holder.load(std::memory_order_acquire);
            }

            void hashpower(size_type val) {
                hashpower_holder.store(val, std::memory_order_release);
            }

            size_type size() const {
                return 1UL << hashpower();
            }

            allocator_type get_allocator() const {
                return allocator;
            }

            bucket& operator[](size_type i) {
                return buckets[i];
            }
            const bucket& operator[](size_type i) const {
                return buckets[i];
            }

            template <typename K, typename... Args>
            void set_element(size_type index, size_type slot, partial_t partial_key,
                             K&& k,
                             Args&&... args) {
                bucket& b = buckets[index];
                assert(!b.occupied(slot));
                b.partial(slot) = partial_key;
                traits::construct(allocator, std::addressof(b.storage_element(slot)),
                                  std::piecewise_construct,
                                  std::forward_as_tuple(std::forward<K>(k)),
                                  std::forward_as_tuple(std::forward<Args>(args)...));
                b.occupied(slot) = true;
            }

            void erase_element(size_type index, size_type slot) {
                bucket& b = buckets[index];
                assert(b.occupied(slot));
                b.occupied(slot) = false;
                traits::destroy(allocator, std::addressof(b.element(slot)));
            }

            void move_element(size_type dst_index, size_type dst_slot,
                              size_type src_index, size_type src_slot) {
                bucket& dst = buckets[dst_index];
                bucket& src = buckets[src_index];
                assert(src.occupied(src_slot));
                assert(!dst.occupied(dst_slot));
                set_element(dst_index, dst_slot, src.partial(src_slot),
                            src.movable_key(src_slot),
                            std::move(src.mapped(src_slot)));
                erase_element(src_index, src_slot);
            }

            void clear() noexcept {
                static_assert(
                    std::is_nothrow_destructible<key_type>::value &&
                    std::is_nothrow_destructible<mapped_type>::value,
                    "bucket_container requires key and value to be nothrow "
                    "destructible");
                for (size_type i = 0; i < size(); ++i) {
                    bucket& b = buckets[i];
                    for (size_type j = 0; j < SLOTS_PER_BUCKET; ++j) {
                        if (b.occupied(j)) {
                            erase_element(i, j);
                        }
                    }
                }
            }

            void resize(size_type new_size) {
                assert(new_size >= hashpower());
                bucket* new_buckets = transfer(new_size, *this, std::true_type());
                destroy_buckets();
                buckets = new_buckets;
                hashpower(new_size);
            }

        private:
            void move_assign(bucket_container& src, std::true_type) {
                allocator = std::move(src.allocator);
                bucket_allocator = allocator;
                hashpower(src.hashpower());
                buckets = src.buckets;
                src.buckets = nullptr;
            }
            void move_assign(bucket_container& src, std::false_type) {
                hashpower(src.hashpower());
                if (allocator == src.allocator) {
                    buckets = src.buckets;
                    src.buckets = nullptr;
                } else {
                    buckets = transfer(src.hashpower(), src, std::true_type());
                }
            }

            void destroy_buckets() noexcept {
                if (buckets == nullptr) {
                    return;
                }
                static_assert(std::is_nothrow_destructible<bucket>::value,
                              "bucket_container requires bucket to be nothrow "
                              "destructible");
                clear();
                for (size_type i = 0; i < size(); ++i) {
                    traits::destroy(allocator, &buckets[i]);
                }
                bucket_allocator.deallocate(buckets, size());
                buckets = nullptr;
            }

            void move_or_copy(size_type dst_index, size_type dst_slot, bucket& src,
                              size_type src_slot, std::true_type) {
                set_element(dst_index, dst_slot, src.partial(src_slot), src.movable_key(src_slot),
                            std::move(src.mapped(src_slot)));
            }
            void move_or_copy(size_type dst_index, size_type dst_slot, bucket& src,
                              size_type src_slot, std::false_type) {
                set_element(dst_index, dst_slot, src.partial(src_slot), src.key(src_slot),
                            src.mapped(src_slot));
            }

            template <bool B>
            bucket* transfer(size_type dst_hashpower,
                             typename std::conditional<B, bucket_container&,
                             const bucket_container&>::type src,
                             std::integral_constant<bool, B> move) {
                assert(dst_hashpower >= src.hashpower());
                bucket_container dst(dst_hashpower, get_allocator());

                for (size_t i = 0; i < src.size(); ++i) {
                    for (size_t j = 0; j < SLOTS_PER_BUCKET; ++j) {
                        if (src.buckets[i].occupied(j)) {
                            dst.move_or_copy(i, j, src.buckets[i], j, move);
                        }
                    }
                }

                bucket* dst_pointer = dst.buckets;
                dst.buckets = nullptr;
                return dst_pointer;
            }

            allocator_type allocator;
            typename traits::template rebind_alloc<bucket> bucket_allocator;
            std::atomic<size_type> hashpower_holder;
            bucket* buckets;
        };

        using LOCKING_ACTIVE = std::integral_constant<bool, true>;
        using LOCKING_INACTIVE = std::integral_constant<bool, false>;

        class alignas(64) spinlock {
        public:
            spinlock()
                : counter(0) {
                flag.clear();
            }

            spinlock(const spinlock& other)
                : counter(other.counter) {
                flag.clear();
            }

            spinlock& operator = (const spinlock& other) {
                counter = other.counter;
                return *this;
            }

            void lock(LOCKING_ACTIVE) noexcept {
                while (flag.test_and_set(std::memory_order_acq_rel));
            }
            void lock(LOCKING_INACTIVE) noexcept {
            }

            void unlock(LOCKING_ACTIVE) noexcept {
                flag.clear(std::memory_order_release);
            }
            void unlock(LOCKING_INACTIVE) noexcept {
            }

            bool try_lock(LOCKING_ACTIVE) noexcept {
                return !flag.test_and_set(std::memory_order_acq_rel);
            }
            bool try_lock(LOCKING_INACTIVE) noexcept {
                return true;
            }

            size_t& elem_counter() noexcept {
                return counter;
            }
            size_t elem_counter() const noexcept {
                return counter;
            }

        private:
            std::atomic_flag flag;
            size_t counter;
        };

        /**
         * A fixed-size array of locks, broken up into segments that are dynamically
         * allocated upon request. It is the user's responsibility to make sure they
         * only access allocated parts of the array.
         *
         * @tparam OFFSET_BITS the number of bits of the index used as the offset within
         * a segment
         * @tparam SEGMENT_BITS the number of bits of the index used as the segment
         * index
         * @tparam Allocator the allocator used to allocate data
         */
        template <uint8_t OFFSET_BITS, uint8_t SEGMENT_BITS, class Allocator>
        class spinlock_dynarray {
        private:
            using traits =
                typename std::allocator_traits<Allocator>::template rebind_traits<spinlock>;

        public:
            using value_type = typename traits::value_type;
            using allocator_type = typename traits::allocator_type;
            using size_type = private_impl::size_type;
            using reference = value_type&;
            using const_reference = const value_type&;

            static_assert(SEGMENT_BITS + OFFSET_BITS <= sizeof(size_type) * 8,
                          "The number of segment and offset bits cannot exceed "
                          " the number of bits in a size_type");

            spinlock_dynarray(size_type target,
                              const allocator_type& allocator = allocator_type())
                : allocator(allocator)
                , segment_allocator(allocator)
                , segments(nullptr)
                , allocated_segments(0) {
                try {
                    segments = segment_allocator.allocate(NUM_SEGMENTS);
                    std::fill(segments, segments + NUM_SEGMENTS, nullptr);
                    resize(target);
                } catch(...) {
                    if (segments != nullptr) {
                        for (auto i = 0; i < NUM_SEGMENTS; ++i) {
                            if (segments[i] != nullptr) {
                                destroy_segment(segments[i]);
                                segments[i] = nullptr;
                            }
                        }
                        segment_allocator.deallocate(segments, NUM_SEGMENTS);
                        segments = nullptr;
                    }
                    throw;
                }
            }

            spinlock_dynarray(const spinlock_dynarray& other)
                : spinlock_dynarray(0, traits::select_on_container_copy_construction(
                                        other.get_allocator())) {
                copy_container(other);
            }
            spinlock_dynarray(const spinlock_dynarray& other,
                              const allocator_type& allocator)
                : spinlock_dynarray(0, allocator) {
                copy_container(other);
            }
            spinlock_dynarray(spinlock_dynarray&& other) noexcept
                : allocator(std::move(other.allocator))
                , segment_allocator(allocator)
                , segments(other.segments)
                , allocated_segments(other.allocated_segments) {
                other.segments = nullptr;
            }
            spinlock_dynarray(spinlock_dynarray&& other,
                              const allocator_type& allocator)
                : spinlock_dynarray(0, allocator) {
                move_assign(other, std::false_type());
            }

            spinlock_dynarray& operator=(const spinlock_dynarray& other) {
                destroy_container();
                copy_allocator(allocator, other.allocator,
                    typename traits::propagate_on_container_copy_assignment());
                segment_allocator = allocator;
                copy_container(other);
                return *this;
            }
            spinlock_dynarray& operator=(spinlock_dynarray&& other) {
                destroy_container();
                move_assign(other,
                            typename traits::propagate_on_container_move_assignment());
                return *this;
            }

            ~spinlock_dynarray() noexcept {
                if (segments != nullptr) {
                    destroy_container();
                    segment_allocator.deallocate(segments, NUM_SEGMENTS);
                }
            }

            void swap(spinlock_dynarray& other) noexcept {
                swap_allocator(allocator, other.allocator,
                               typename traits::propagate_on_container_swap());
                segment_allocator = allocator;
                std::swap(segments, other.segments);
                std::swap(allocated_segments, other.allocated_segments);
            }

            void emulate(const spinlock_dynarray& other) noexcept {
                resize(other.size());
                for (size_type i = 0; i < allocated_segments; ++i) {
                    emulate_segment(segments[i], other.segments[i]);
                }
            }

            reference operator[](size_type i) {
                assert(get_segment(i) < allocated_segments);
                return segments[get_segment(i)][get_offset(i)];
            }

            const_reference operator[](size_type i) const {
                assert(get_segment(i) < allocated_segments);
                return segments[get_segment(i)][get_offset(i)];
            }

            size_type size() const {
                if (segments == nullptr) {
                    return 0;
                }
                return allocated_segments * SEGMENT_SIZE;
            }

            static constexpr size_type max_size() {
                return 1UL << (OFFSET_BITS + SEGMENT_BITS);
            }

            void resize(size_type target) {
                target = std::min(target, max_size());
                const size_type last_segment = get_segment(target - 1);
                for (size_type i = allocated_segments; i <= last_segment; ++i) {
                    segments[i] = create_segment();
                }
                allocated_segments = last_segment + 1;
            }

        private:
            static constexpr size_type SEGMENT_SIZE = 1UL << OFFSET_BITS;
            static constexpr size_type NUM_SEGMENTS = 1UL << SEGMENT_BITS;
            static constexpr size_type OFFSET_MASK = SEGMENT_SIZE - 1;

            spinlock* create_segment() {
                spinlock* segment = traits::allocate(allocator, SEGMENT_SIZE);
                for (size_type i = 0; i < SEGMENT_SIZE; ++i) {
                    segment[i].unlock(LOCKING_ACTIVE());
                    segment[i].elem_counter() = 0;
                }
                return segment;
            }

            void destroy_segment(spinlock* segment) {
                traits::deallocate(allocator, segment, SEGMENT_SIZE);
            }

            static void emulate_segment(spinlock* dst, spinlock* src) {
                for (size_type i = 0; i < SEGMENT_SIZE; ++i) {
                    dst[i].elem_counter() = src[i].elem_counter();
                }
            }

            spinlock* copy_segment(spinlock* src) {
                spinlock* dst = create_segment();
                emulate_segment(dst, src);
                return dst;
            }

            void copy_container(const spinlock_dynarray& other) {
                std::fill(segments, segments + NUM_SEGMENTS, nullptr);
                allocated_segments = other.allocated_segments;
                for (size_type i = 0; i < allocated_segments; ++i) {
                    segments[i] = copy_segment(other.segments[i]);
                }
            }

            void move_assign(spinlock_dynarray&& other, std::true_type) {
                allocator = std::move(other.allocator);
                segment_allocator = allocator;
                segments = other.segments;
                allocated_segments = other.allocated_segments;
                other.segments = nullptr;
            }

            void move_assign(spinlock_dynarray& other, std::false_type) {
                if (allocator == other.allocator) {
                    segments = other.segments;
                    allocated_segments = other.allocated_segments;
                    other.segments = nullptr;
                } else {
                    copy_container(other);
                }
            }

            void destroy_container() {
                if (segments != nullptr) {
                    for (size_type i = 0; i < allocated_segments; ++i) {
                        destroy_segment(segments[i]);
                    }
                    std::fill(segments, segments + NUM_SEGMENTS, nullptr);
                    allocated_segments = 0;
                }
            }

            static size_type get_segment(size_type i) {
                return i >> OFFSET_BITS;
            }

            static size_type get_offset(size_type i) {
                return i & OFFSET_MASK;
            }

            allocator_type allocator;
            typename traits::template rebind_alloc<spinlock*> segment_allocator;
            spinlock** segments;
            size_type allocated_segments;
        };

        struct hash_value {
            size_type hash;
            partial_t partial;
        };

        // node holds one position in a cuckoo path. Since cuckoopath
        // elements only define a sequence of alternate hashings for different hash
        // values, we only need to keep track of the hash values being moved, rather
        // than the keys themselves.
        typedef struct {
            size_type bucket;
            size_type slot;
            hash_value hv;
        } node;

        static constexpr uint8_t MAX_BFS_PATH_LEN = 5;

        using nodes = std::array<node, MAX_BFS_PATH_LEN>;

        static constexpr size_type const_pow(size_type a, size_type b) {
            return (b == 0) ? 1 : a * const_pow(a, b - 1);
        }

        // bfs_slot holds the information for a BFS path through the table.
#pragma pack(push, 1)
        template<private_impl::size_type SLOTS_PER_BUCKET>
        struct bfs_slot {
            using size_type = private_impl::size_type;
            // The bucket of the last item in the path.
            size_type bucket;
            // a compressed representation of the slots for each of the buckets in
            // the path. pathcode is sort of like a base-slot_per_bucket number, and
            // we need to hold at most MAX_BFS_PATH_LEN slots. Thus we need the
            // maximum pathcode to be at least slot_per_bucket()^(MAX_BFS_PATH_LEN).
            size_type pathcode;
            static_assert(const_pow(SLOTS_PER_BUCKET, MAX_BFS_PATH_LEN) <
                          std::numeric_limits<decltype(pathcode)>::max(),
                          "pathcode may not be large enough to encode a cuckoo "
                          "path");
            // The 0-indexed position in the cuckoo path this slot occupies. It must
            // be less than MAX_BFS_PATH_LEN, and also able to hold negative values.
            int_fast8_t depth;
            static_assert(MAX_BFS_PATH_LEN - 1 <=
                          std::numeric_limits<decltype(depth)>::max(),
                          "The depth type must able to hold a value of"
                          " MAX_BFS_PATH_LEN - 1");
            static_assert(-1 >= std::numeric_limits<decltype(depth)>::min(),
                          "The depth type must be able to hold a value of -1");
            bfs_slot() {}
            bfs_slot(const size_type b, const size_type p, const decltype(depth) d)
                : bucket(b)
                , pathcode(p)
                , depth(d) {
                assert(d < MAX_BFS_PATH_LEN);
            }
        };
#pragma pack(pop)

// bfs_queue is the queue used to store bfs_slots for BFS cuckoo hashing.
#pragma pack(push, 1)
        template<private_impl::size_type SLOTS_PER_BUCKET>
        class bfs_queue {
        public:
            bfs_queue() noexcept : first(0), last(0)
                {
                }

            void enqueue(bfs_slot<SLOTS_PER_BUCKET> x) {
                assert(!full());
                slots[last] = x;
                last = increment(last);
            }

            bfs_slot<SLOTS_PER_BUCKET> dequeue() {
                assert(!empty());
                bfs_slot<SLOTS_PER_BUCKET> &x = slots[first];
                first = increment(first);
                return x;
            }

            bool empty() const {
                return first == last;
            }

            bool full() const {
                return increment(last) == first;
            }

        private:
            // The maximum size of the BFS queue. Note that unless it's less than
            // slot_per_bucket()^MAX_BFS_PATH_LEN, it won't really mean anything.
            static constexpr size_type MAX_CUCKOO_COUNT = 256;
            static_assert((MAX_CUCKOO_COUNT & (MAX_CUCKOO_COUNT - 1)) == 0,
                          "MAX_CUCKOO_COUNT should be a power of 2");
            // A circular array of bfs_slots
            bfs_slot<SLOTS_PER_BUCKET> slots[MAX_CUCKOO_COUNT];
            // The index of the head of the queue in the array
            size_type first;
            // One past the index of the last_ item of the queue in the array.
            size_type last;

            // returns the index in the queue after ind, wrapping around if
            // necessary.
            size_type increment(size_type index) const {
                return (index + 1) & (MAX_CUCKOO_COUNT - 1);
            }
        };
#pragma pack(pop)


        /**
         * Thrown when an automatic expansion is triggered, but the load factor of the
         * table is below a minimum threshold, which can be set by the \ref
         * cuckoohash_map::minimum_load_factor method. This can happen if the hash
         * function does not properly distribute keys, or for certain adversarial
         * workloads.
         */
        class load_factor_too_low : public std::exception {
        public:
            load_factor_too_low(const double loadfactor)
                : loadfactor(loadfactor)
                {
                }
            virtual const char* what() const noexcept override {
                return "Automatic expansion triggered when load factor was below "
                    "minimum threshold";
            }

            /**
             * @return the load factor of the table when the exception was thrown
             */
            double load_factor() const { return loadfactor; }

        private:
            const double loadfactor;
        };

        /**
         * Thrown when an expansion is triggered, but the hashpower specified is greater
         * than the maximum, which can be set with the \ref
         * cuckoohash_map::maximum_hashpower method.
         */
        class maximum_hashpower_exceeded : public std::exception {
        public:
            maximum_hashpower_exceeded(const size_t hash_power)
                : hash_power(hash_power)
                {
                }
            virtual const char* what() const noexcept override {
                return "Expansion beyond maximum hashpower";
            }
            size_t hashpower() const {
                return hash_power;
            }

        private:
            const size_t hash_power;
        };
    }

    template <class Key,
              class Value,
              class Hasher = std::hash<Key>,
              class Equality = std::equal_to<Key>,
              class Allocator = std::allocator<pair<const Key, Value>> >
    class concurrent_unordered_map {
    public:
        // types:
        using key_type          = Key;
        using mapped_type       = Value;
        using value_type        = pair<const Key, Value>;
        using hasher            = Hasher;
        using key_equal         = Equality;
        using allocator_type    = Allocator;
        using size_type         = std::size_t;

    private:
        using partial_t = private_impl::partial_t;
        using buckets_t = private_impl::bucket_container<Key, Value,
                                                         Allocator, partial_t,
                                                         private_impl::DEFAULT_SLOTS_PER_BUCKET>;
        using bucket = typename buckets_t::bucket;

        template <typename LOCK_TYPE>
        class all_buckets_guard;

    public:

        class unordered_map_view {
            std::reference_wrapper<concurrent_unordered_map<Key, Value, Hasher, Equality, Allocator>> delegate;
            all_buckets_guard<std::private_impl::LOCKING_ACTIVE> guard;

        public:
            // types:
            using key_type          = Key;
            using mapped_type       = Value;
            using value_type        = pair<const Key, Value>;
            using hasher            = Hasher;
            using key_equal         = Equality;
            using allocator_type    = Allocator;

            using pointer           = typename allocator_traits<Allocator>::pointer;
            using const_pointer     = typename allocator_traits<Allocator>::const_pointer;
            using reference         = value_type&;
            using const_reference   = const value_type&;
            using size_type         = concurrent_unordered_map<Key, Value, Hasher, Equality, Allocator>::size_type;
            using difference_type   = std::ptrdiff_t;

            class const_local_iterator {
            public:
                using difference_type = unordered_map_view::difference_type;
                using value_type = unordered_map_view::value_type;
                using pointer = unordered_map_view::const_pointer;
                using reference = unordered_map_view::const_reference;
                using iterator_category = std::bidirectional_iterator_tag;

                const_local_iterator() = default;

                bool operator==(const const_local_iterator& other) const {
                    return data == other.data && slot == other.slot;
                }

                bool operator!=(const const_local_iterator& other) const {
                    return !(operator==(other));
                }

                reference operator*() const {
                    return data->element(slot);
                }

                pointer operator->() const {
                    return std::addressof(operator*());
                }

                const_local_iterator& operator++() {
                    if (slot < private_impl::DEFAULT_SLOTS_PER_BUCKET) {
                        ++slot;
                        while (slot < private_impl::DEFAULT_SLOTS_PER_BUCKET &&
                               !data->occupied(slot)) {
                            ++slot;
                        }
                    }

                    return *this;
                }

                const_local_iterator operator++(int) {
                    const_local_iterator old(*this);
                    ++(*this);
                    return std::move(old);
                }

                const_local_iterator operator--() {
                    --slot;
                    while(!data->occupied(slot)) {
                        --slot;
                    }
                    return *this;
                }

                const_local_iterator operator--(int) {
                    const_local_iterator old(*this);
                    --(*this);
                    return std::move(old);
                }

            protected:
                const_local_iterator(bucket* data, size_type slot) noexcept
                    : data(data)
                    , slot(slot) {
                    if (slot < private_impl::DEFAULT_SLOTS_PER_BUCKET &&
                        !data->occupied(slot)) {
                        operator++();
                    }
                }

                static const_local_iterator begin(bucket* data) {
                    return const_local_iterator(data, 0);
                }

                static const_local_iterator end(bucket* data) {
                    return const_local_iterator(data, private_impl::DEFAULT_SLOTS_PER_BUCKET);
                }

                bucket* data;
                size_type slot;
                friend class bucket_iterator;
                friend class unordered_map_view;
            };

            class local_iterator : public const_local_iterator {
            public:
                using pointer = unordered_map_view::pointer;
                using reference = unordered_map_view::reference;

                local_iterator() {}

                bool operator==(const local_iterator& it) const {
                    return const_local_iterator::operator==(it);
                }

                bool operator!=(const local_iterator& it) const {
                    return const_local_iterator::operator!=(it);
                }

                reference operator*() {
                    return const_local_iterator::data->element(const_local_iterator::slot);
                }

                pointer operator->() {
                    return std::addressof(operator*());
                }

                const_reference operator*() const {
                    return const_local_iterator::data->element(const_local_iterator::slot);
                }

                const_pointer operator->() const {
                    return std::addressof(operator*());
                }


                local_iterator& operator++() {
                    const_local_iterator::operator++();
                    return *this;
                }

                local_iterator operator++(int) {
                    iterator old(*this);
                    const_local_iterator::operator++();
                    return std::move(old);
                }

                local_iterator& operator--() {
                    const_local_iterator::operator--();
                    return *this;
                }

                local_iterator operator--(int) {
                    local_iterator old(*this);
                    const_local_iterator::operator--();
                    return std::move(old);
                }

            private:
                local_iterator(bucket* data, size_type slot) noexcept
                   : const_local_iterator(data, slot)
                    {
                    }

                static local_iterator begin(bucket* data) {
                    return local_iterator(data, 0);
                }

                static local_iterator end(bucket* data) {
                    return local_iterator(data, private_impl::DEFAULT_SLOTS_PER_BUCKET);
                }

                friend class bucket_iterator;
                friend class unordered_map_view;
            };

            class const_iterator {
            public:
                using difference_type = unordered_map_view::difference_type;
                using value_type = unordered_map_view::value_type;
                using pointer = unordered_map_view::const_pointer;
                using reference = unordered_map_view::const_reference;
                using iterator_category = std::bidirectional_iterator_tag;

                const_iterator() {}

                bool operator==(const const_iterator& other) const {
                    return buckets == other.buckets && bucket_index == other.bucket_index &&
                        (bucket_position == other.bucket_position || bucket_index == buckets->size());
                }

                bool operator!=(const const_iterator& it) const {
                    return !const_iterator::operator==(it);
                }

                reference operator*() const {
                    return *bucket_position;
                }

                pointer operator->() const {
                    return bucket_position.operator->();
                }

                const_iterator& operator++() {
                    increment();
                    return *this;
                }

                const_iterator operator++(int) {
                    const_iterator old(*this);
                    ++(*this);
                    return std::move(old);
                }

                const_iterator& operator--() {
                    decrement();
                    return *this;
                }

                const_iterator operator--(int) {
                    const_iterator old(*this);
                    --(*this);
                    return std::move(old);
                }

            protected:
                void increment() {
                    ++bucket_position;
                    if (bucket_position == local_iterator::end(&((*buckets)[bucket_index]))) {
                        ++bucket_index;
                        while (bucket_index < buckets->size()) {
                            bucket_position = local_iterator::begin(&((*buckets)[bucket_index]));
                            if (bucket_position != local_iterator::end(&((*buckets)[bucket_index]))) {
                                break;
                            }
                            ++bucket_index;
                        }
                    }
                }

                void decrement() {
                    if (bucket_index == buckets->size() ||
                            local_iterator::begin(&((*buckets)[bucket_index])) == bucket_position) {
                        --bucket_index;
                        while (local_iterator::begin(&((*buckets)[bucket_index])) ==
                               local_iterator::end(&((*buckets)[bucket_index]))) {
                            --bucket_index;
                        }
                        bucket_position = local_iterator::end(&((*buckets)[bucket_index]));
                        --bucket_position;
                    } else {
                        --bucket_position;
                    }
                }

                const_iterator(buckets_t* buckets, size_type bucket_index, size_type slot)
                    : buckets(buckets)
                    , bucket_index(bucket_index)
                    , bucket_position(&(*buckets)[bucket_index != buckets->size() ? bucket_index : 0],
                                      slot)
                    {
                        if (bucket_index < buckets->size() && bucket_position == local_iterator::end(&(*buckets)[bucket_index])) {
                            increment();
                        }
                    }

                const_local_iterator begin(bucket* bucket) {
                    return const_local_iterator::begin(bucket);
                }

                const_local_iterator end(bucket* bucket) {
                    return const_local_iterator::end(bucket);
                }

                buckets_t* buckets;
                size_type bucket_index;
                local_iterator bucket_position;
                friend class unordered_map_view;
            };

            class iterator : public const_iterator {
            public:
                using difference_type = unordered_map_view::difference_type;
                using value_type = unordered_map_view::value_type;
                using pointer = unordered_map_view::pointer;
                using const_pointer = unordered_map_view::const_pointer;
                using reference = unordered_map_view::reference;
                using const_reference = unordered_map_view::const_reference;
                using iterator_category = std::bidirectional_iterator_tag;

                iterator() {}

                bool operator==(const iterator& other) const {
                    return this->buckets == other.buckets && this->bucket_index == other.bucket_index &&
                        (this->bucket_position == other.bucket_position || this->bucket_index == this->buckets->size());
                }
                bool operator!=(const iterator& it) const {
                    return !operator==(it);
                }

                const_reference operator*() const {
                    return *this->bucket_position;
                }
                reference operator*() {
                    return *this->bucket_position;
                }

                const_pointer operator->() const {
                    return this->bucket_position.operator->();
                }
                pointer operator->() {
                    return this->bucket_position.operator->();
                }

                iterator operator++() {
                    const_iterator::increment();
                    return *this;
                }
                iterator& operator++(int) {
                    iterator old(*this);
                    const_iterator::increment();
                    return std::move(old);
                }

                iterator operator--() {
                    const_iterator::decrement();
                    return *this;
                }
                iterator& operator--(int) {
                    iterator old(*this);
                    --(*this);
                    return std::move(old);
                }

            private:
                iterator(buckets_t* buckets, size_type bucket_index, size_type slot)
                    : const_iterator(buckets, bucket_index, slot)
                    {
                    }

                local_iterator begin(bucket* bucket) {
                    return local_iterator::begin(bucket);
                }

                local_iterator end(bucket* bucket) {
                    return local_iterator::end(bucket);
                }

                friend class unordered_map_view;
            };

            // construct/copy/destroy:
            unordered_map_view() = delete;
            unordered_map_view(concurrent_unordered_map<Key, Value, Hasher, Equality, Allocator>& delegate)
                : delegate(delegate)
                {
                }

            // iterators:
            iterator begin() noexcept {
                return iterator(&delegate.get().buckets, 0, 0);
            }
            const_iterator begin() const noexcept {
                return const_iterator(&delegate.get().buckets, 0, 0);
            }
            iterator end() noexcept {
                return iterator(&delegate.get().buckets, delegate.get().buckets.size(), 0);
            }
            const_iterator end() const noexcept {
                return const_iterator(&delegate.get().buckets, delegate.get().buckets.size(), 0);
            }
            const_iterator cbegin() const noexcept {
                return const_iterator(&delegate.get().buckets, 0, 0);
            }
            const_iterator cend() const noexcept {
                return const_iterator(&delegate.get().buckets, delegate.get().buckets.size(), 0);
            }

            // capacity:
            bool empty() const noexcept {
                return delegate.get().size() == 0;
            }

            size_type size() const noexcept {
                return delegate.get().size();
            }

            size_type max_size() const noexcept {
                return 1U << delegate.get().buckets->maximum_hashpower();
            }

            bool operator==(const unordered_map_view& other) const {
                if (size() != other.size()) {
                    return false;
                }
                for (const auto& elem : other) {
                    auto it = find(elem.first);
                    if (it == end() || it->second != elem.second) {
                        return false;
                    }
                }
                return true;
            }

            bool operator!=(const unordered_map_view& other) const {
                if (size() != other.size()) {
                    return true;
                }
                for (const auto& elem : other) {
                    auto it = find(elem.first);
                    if (it == end() || it->second != elem.second) {
                        return true;
                    }
                }
                return false;
            }

            // modifiers:
            template<typename... Args>
            pair<iterator, bool> emplace(Args&&... args) {
                value_type x(std::forward(args)...);
                return insert(std::move(x));
            }

            pair<iterator, bool> insert(const value_type& x) {
                hash_value hv = delegate.get().hashed_key(x.first);
                auto b = delegate.get().template snapshot_and_lock_two<private_impl::LOCKING_INACTIVE>(hv);
                table_position pos = delegate.get().cuckoo_insert_loop(hv, b, x.first);
                if (pos.status == ok) {
                    delegate.get().add_to_bucket(pos.index, pos.slot, hv.partial,
                                                 x.first,
                                                 x.second);
                } else {
                    assert(pos.status == failure_key_duplicated);
                }
                return std::make_pair(iterator(&delegate.get().buckets, pos.index, pos.slot),
                                               pos.status == ok);
            }
            pair<iterator, bool> insert(value_type&& x) {
                hash_value hv = delegate.get().hashed_key(x.first);
                auto b = delegate.get().template snapshot_and_lock_two<private_impl::LOCKING_INACTIVE>(hv);
                table_position pos = delegate.get().cuckoo_insert_loop(hv, b, x.first);
                if (pos.status == ok) {
                    delegate.get().add_to_bucket(pos.index, pos.slot, hv.partial,
                                                 std::move(const_cast<key_type&>(x.first)),
                                                 std::move(x.second));
                } else {
                    assert(pos.status == failure_key_duplicated);
                }
                return std::make_pair(iterator(&delegate.get().buckets, pos.index, pos.slot),
                                               pos.status == ok);
            }
            void insert(initializer_list<value_type> il) {
                for (auto pos = il.begin(); pos != il.end(); ++pos)  {
                    insert(*pos);
                }
            }
            template<class InputIterator>
            void insert(InputIterator first, InputIterator last) {
                for (auto i = first; i != last; ++i) {
                    insert(*i);
                }
            }

            template <class M>
            pair<iterator, bool> insert_or_assign(const key_type& key, M&& obj) {
                hash_value hv = delegate.get().hashed_key(key);
                auto b = delegate.get().template snapshot_and_lock_two<private_impl::LOCKING_INACTIVE>(hv);
                table_position pos = delegate.get().cuckoo_insert_loop(hv, b, key);
                if (pos.status == ok) {
                    delegate.get().add_to_bucket(pos.index, pos.slot, hv.partial,
                                           key,
                                           std::forward<M>(obj));
                } else {
                    assert(pos.status == failure_key_duplicated);
                    b.element(pos.slot) = std::forward<M>(obj);
                }
                return std::make_pair(iterator(delegate.get().buckets, pos.index, pos.slot),
                                      pos.status == ok);
            }
            template <class M>
            pair<iterator, bool> insert_or_assign(key_type&& key, M&& obj) {
                hash_value hv = delegate.get().hashed_key(key);
                auto b = delegate.get().template snapshot_and_lock_two<private_impl::LOCKING_INACTIVE>(hv);
                table_position pos = delegate.get().cuckoo_insert_loop(hv, b, key);
                if (pos.status == ok) {
                    delegate.get().add_to_bucket(pos.index, pos.slot, hv.partial,
                                           std::forward<key_type>(key),
                                           std::forward<M>(obj));
                } else {
                    assert(pos.status == failure_key_duplicated);
                    b.element(pos.slot) = std::forward<M>(obj);
                }
                return std::make_pair(iterator(delegate.get().buckets, pos.index, pos.slot),
                                      pos.status == ok);

            }

            iterator erase(iterator position) {
                delegate.get().del_from_bucket(position.bucket_index, position.bucket_position.slot);
                iterator result(&delegate.get().buckets, position.bucket_index, position.bucket_position.slot);
                return result;
            }
            iterator erase(const_iterator position) {
                delegate.get().del_from_bucket(position.bucket_index, position.bucket_position.slot);
                iterator result(&delegate.get().buckets, position.bucket_index, position.bucket_position.slot);
                return result;
            }
            size_type erase(const key_type& key) {
                const hash_value hv = delegate.get().hashed_key(key);
                const auto guard = delegate.get().template snapshot_and_lock_two<private_impl::LOCKING_INACTIVE>(hv);
                const table_position pos =
                    delegate.get().cuckoo_find(key, hv.partial, guard.first(), guard.second());
                if (pos.status == ok) {
                    delegate.get().del_from_bucket(pos.index, pos.slot);
                    return 1;
                } else {
                    return 0;
                }
            }

            iterator erase(const_iterator first, const_iterator last) {
                for (auto pos = first; pos != last; ++pos) {
                    erase(pos);
                }
                return iterator(delegate.get().buckets, last.bucket_index, last.slot);
            }

            void swap(unordered_map_view& other) noexcept {
                guard.swap(other.guard);
                delegate.swap(other);
            }

            void clear() noexcept {
                delegate.get().buckets.clear();
                auto& locks = delegate.get().get_current_locks();
                for (size_type i = 0; i < locks.size(); ++i) {
                    locks[i].elem_counter() = 0;
                }
            }

            template<class H2, class P2>
            void merge(concurrent_unordered_map<Key, Value, H2, P2, Allocator>& source) {
                auto locked_table = source.lock_table();
                insert(locked_table.begin(), locked_table.end());
            }

            template<class H2, class P2>
            void merge(concurrent_unordered_map<Key, Value, H2, P2, Allocator>&& source) {
                auto locked_table = source.lock_table();
                insert(locked_table.begin(), locked_table.end());
            }

            // observers:
            allocator_type get_allocator() const {
                return delegate.get().get_allocator();
            }
            hasher hash_function() const {
                return delegate.get().hash_function();
            }
            key_equal key_eq() const {
                return delegate.get().key_eq();
            }

            // map operations:
            iterator find(const key_type& key) {
                const hash_value hashvalue = delegate.get().hashed_key(key);
                const auto guard = delegate.get().template snapshot_and_lock_two<private_impl::LOCKING_INACTIVE>(hashvalue);
                const table_position pos = delegate.get().cuckoo_find(key, hashvalue.partial,
                                                                guard.first(), guard.second());
                if (pos.status == ok) {
                    return iterator(&delegate.get().buckets, pos.index, pos.slot);
                } else {
                    return end();
                }
            }
            const_iterator find(const key_type& key) const {
                const hash_value hashvalue = delegate.get().hashed_key(key);
                const auto guard = delegate.get().template snapshot_and_lock_two<private_impl::LOCKING_INACTIVE>(hashvalue);
                const table_position pos = delegate.get().cuckoo_find(key, hashvalue.partial,
                                                                guard.first(), guard.second());
                if (pos.status == ok) {
                    return const_iterator(&delegate.get().buckets, pos.index, pos.slot);
                } else {
                    return cend();
                }
            }
            size_type count(const key_type& key) const {
                return find(key) != cend();
            }
            pair<iterator, iterator> equal_range(const key_type& key) {
                //DO we really need it?
                auto start = find(key);
                auto end = start;
                ++end;
                return make_pair(start, end);
            }
            pair<const_iterator, const_iterator> equal_range(const key_type& key) const {
                //DO we really need it?
                auto start = find(key);
                auto end = start;
                ++end;
                return make_pair(start, end);
            }

            // element access:
            mapped_type& operator[](const key_type& key) {
                auto result = insert(value_type(key, {}));
                return result.first->second;
            }
            mapped_type& operator[](key_type&& key) {
                auto result = insert(std::move(value_type(std::forward<key_type>(key), {})));
                return result.first->second;
            }
            const mapped_type& at(const key_type& key) const {
                auto it = find(key);
                if (it == end()) {
                    throw std::out_of_range("key not found in table");
                } else {
                    return it->second;
                }
            }
            mapped_type& at(const key_type& key) {
                auto it = find(key);
                if (it == end()) {
                    throw std::out_of_range("key not found in table");
                } else {
                    return it->second;
                }
            }

            size_type bucket_count() const {
                return delegate.get().buckets.size();
            }

            size_type max_bucket_count() const {
                return std::numeric_limits<size_type>::max() / SLOTS_PER_BUCKET;
            }

            size_type bucket_size(size_type n) {
                size_type result = 0;
                const auto& bucket = delegate.get().buckets[n];
                for (size_type i = 0; i < SLOTS_PER_BUCKET; ++i) {
                    if (bucket.occupied(i)) {
                        ++result;
                    }
                }
                return result;
            }
            size_type bucket(const key_type& key) const {
                const hash_value hv = delegate.get().hashed_key(key);
                const auto guard = delegate.get().template snapshot_and_lock_two<private_impl::LOCKING_INACTIVE>(hv);
                const table_position pos =
                    delegate.get().cuckoo_find(key, hv.partial, guard.first(), guard.second());
                return bucket_size(pos.index);
            }

            local_iterator begin(size_type n) {
                return local_iterator::begin(&delegate.get().buckets[n]);
            }
            const_local_iterator begin(size_type n) const {
                return const_local_iterator::begin(&delegate.get().buckets[n]);
            }
            local_iterator end(size_type n) {
                return local_iterator::end(&delegate.get().buckets[n]);
            }
            const_local_iterator end(size_type n) const {
                return const_local_iterator::end(&delegate.get().buckets[n]);
            }
            const_local_iterator cbegin(size_type n) const {
                return const_local_iterator::begin(&delegate.get().buckets[n]);
            }
            const_local_iterator cend(size_type n) const {
                return const_local_iterator::end(&delegate.get().buckets[n]);
            }

            // hash policy:
            void rehash(size_type n) {
                delegate.get().template cuckoo_expand_simple<private_impl::LOCKING_INACTIVE, manual_resize>(n);
            }

            float load_factor() const noexcept {
                return delegate.get().load_factor();
                return delegate.get().load_factor();
            }

        private:
            unordered_map_view(concurrent_unordered_map<Key, Value, Hasher, Equality, Allocator>& delegate,
                               all_buckets_guard<std::private_impl::LOCKING_ACTIVE>&& guard)
                    : delegate(delegate)
                    , guard(std::forward<all_buckets_guard<std::private_impl::LOCKING_ACTIVE>>(guard))
            {
            }
            friend concurrent_unordered_map;
        };

        // construct/destroy:
        explicit concurrent_unordered_map(size_type n = 16,
                                          const hasher& hash = hasher(),
                                          const key_equal& key_comparator = key_equal(),
                                          const allocator_type& allocator = allocator_type())
            : allocator(allocator)
            , hash(hash)
            , key_comparator(key_comparator)
            , buckets(reserve_calc(n), allocator)
            , all_locks(allocator)
            , minimum_load_factor_holder(private_impl::DEFAULT_MINIMUM_LOAD_FACTOR)
            , maximum_hash_power_holder(private_impl::NO_MAXIMUM_HASHPOWER)
            {
                std::private_impl::spinlock x;
                all_locks.emplace_back(std::min(bucket_count(), size_type(std::private_impl::MAX_NUM_LOCKS)),
                                   std::private_impl::spinlock(), allocator);
            }
        template <typename InputIterator>
        concurrent_unordered_map(InputIterator first, InputIterator last,
                                 size_type n = private_impl::DEFAULT_SIZE,
                                 const hasher& hash = hasher(),
                                 const key_equal& key_comparator = key_equal(),
                                 const allocator_type& allocator = allocator_type())
            : allocator(allocator)
            , hash(hash)
            , key_comparator(key_comparator)
            , buckets(reserve_calc(n), allocator)
            , all_locks(allocator)
            , minimum_load_factor_holder(private_impl::DEFAULT_MINIMUM_LOAD_FACTOR)
            , maximum_hash_power_holder(private_impl::NO_MAXIMUM_HASHPOWER)
            {
                all_locks.emplace_back(std::min(n, size_type(std::private_impl::MAX_NUM_LOCKS)),
                                   std::private_impl::spinlock(), get_allocator());
                for (auto i = first; i != last; ++i) {
                    emplace(i->first, i->second);
                }
            }
        concurrent_unordered_map(const allocator_type& allocator)
            : allocator(allocator)
            {
            }
        concurrent_unordered_map(concurrent_unordered_map&& source)
            : allocator(std::move(source.allocator))
            , hash(std::move(source.hash))
            , key_comparator(std::move(source.key_comparator))
            , buckets(std::move(source.buckets), std::move(source.allocator))
            , all_locks(std::move(source.all_locks))
            , minimum_load_factor_holder(source.minimum_load_factor_holder.
                                         load(std::memory_order_acquire))
            , maximum_hash_power_holder(source.maximum_hash_power_holder.
                                       load(std::memory_order_acquire))
        {
        }
        concurrent_unordered_map(concurrent_unordered_map&& source, const allocator_type& allocator)
            : allocator(std::move(allocator))
            , hash(std::move(source.hash))
            , key_comparator(std::move(source.key_comparator))
            , buckets(std::move(source.buckets), allocator)
            , all_locks(std::move(source.locks), allocator)
            , minimum_load_factor_holder(source.minimum_load_factor_holder.
                                         load(std::memory_order_acquire))
            , maximum_hash_power_holder(source.maximum_hash_power_holder.
                                       load(std::memory_order_acquire))
        {
        }
        concurrent_unordered_map(initializer_list<value_type> il,
                                 size_type n = private_impl::DEFAULT_SIZE,
                                 const hasher& hash = hasher(),
                                 const key_equal& key_comparator = key_equal(),
                                 const allocator_type& allocator = allocator_type())
            : allocator(allocator)
            , hash(hash)
            , key_comparator(key_comparator)
            , buckets(reserve_calc(n), allocator)
            , all_locks(allocator)
            , minimum_load_factor_holder(private_impl::DEFAULT_MINIMUM_LOAD_FACTOR)
            , maximum_hash_power_holder(private_impl::NO_MAXIMUM_HASHPOWER)
            {
                all_locks.emplace_back(std::min(n, size_type(std::private_impl::MAX_NUM_LOCKS)),
                                   std::private_impl::spinlock(), get_allocator());
                for (auto i = il.begin(); i != il.end(); ++i) {
                    emplace(i->first, i->second);
                }
            }

        ~concurrent_unordered_map() = default;

        unordered_map_view make_unordered_map_view(bool lock = false) noexcept {
            if (lock) {
                auto guard = snapshot_and_lock_all<std::private_impl::LOCKING_ACTIVE>();
                return unordered_map_view(*this, std::move(guard));
            } else {
                return unordered_map_view(*this);
            }
        }

        // concurrent-safe assignment:
        concurrent_unordered_map& operator=(concurrent_unordered_map&& source) noexcept {
            if (this != &source) {
                this->allocator = std::move(source.allocator);
                this->hash = std::move(source.hash);
                this->key_comparator = std::move(source.key_comparator);
                this->buckets = std::move(source.buckets);
                this->all_locks = std::move(source.all_locks);
                this->minimum_load_factor_holder.store(source.minimum_load_factor_holder.
                                                       load(std::memory_order_acquire),
                                                       std::memory_order_release);
                this->maximum_hash_power_holder.store(source.maximum_hash_power_holder.
                                                     load(std::memory_order_acquire),
                                                     std::memory_order_release);

            }
            return *this;
        }

        concurrent_unordered_map& operator=(initializer_list<value_type> il) {
            {
                auto locked_table = make_unordered_map_view(true);
                locked_table.clear();
                locked_table.insert(il.begin(), il.end());
            }
            return *this;
        }

        allocator_type get_allocator() const {
            return allocator;
        }

        hasher hash_function() const {
            return hash;
        }

        key_equal key_eq() const {
            return key_comparator;
        }

        // concurrent-safe element retrieval:
        experimental::optional<mapped_type> find(const key_type& key) const {
            const hash_value hashvalue = hashed_key(key);
            const auto guard = snapshot_and_lock_two<private_impl::LOCKING_ACTIVE>(hashvalue);
            const table_position pos = cuckoo_find(key, hashvalue.partial,
                                                   guard.first(), guard.second());
            if (pos.status == ok) {
                return experimental::make_optional(buckets[pos.index].mapped(pos.slot));
            }
            return {};
        }

        mapped_type find(const key_type& key, const mapped_type& default_value) const {
            return find(key).value_or(default_value);
        }

        // concurrent-safe modifiers:
        template <typename F>
        bool visit(const key_type& key, F functor) {
            const hash_value hashvalue = hashed_key(key);
            const auto guard = snapshot_and_lock_two<private_impl::LOCKING_ACTIVE>(hashvalue);
            const table_position pos = cuckoo_find(key, hashvalue.partial,
                                                   guard.first(), guard.second());
            if (pos.status == ok) {
                functor(buckets[pos.index].mapped(pos.slot));
                return true;
            }
            return false;
        }

        template<typename F>
        void visit_all(F functor) {
            locks_t& locks = get_current_locks();
            for (auto i = 0; i < locks.size(); ++i) {
                bucket_guard guard(&locks, i);
                const auto& bucket = buckets[i];

                for (size_type j = 0; j < SLOTS_PER_BUCKET; ++j) {
                    if (bucket.occupied(i)) {
                        functor(bucket.element(i));
                    }
                }
            }
        }

        template<typename F>
        void visit_all(F functor) const {
            locks_t& locks = get_current_locks();
            for (auto i = 0; i < locks.size(); ++i) {
                bucket_guard guard(&locks, i);
                const auto& bucket = buckets[i];

                for (size_type j = 0; j < SLOTS_PER_BUCKET; ++j) {
                    if (bucket.occupied(i)) {
                        functor(bucket.element(i));
                    }
                }
            }
        }

        template <typename K, typename F, typename... Args>
        bool emplace_or_visit(K&& key, F functor, Args&&... val) {
            hash_value hv = hashed_key(key);
            auto b = snapshot_and_lock_two<private_impl::LOCKING_ACTIVE>(hv);
            table_position pos = cuckoo_insert_loop(hv, b, key);
            if (pos.status == ok) {
                add_to_bucket(pos.index, pos.slot, hv.partial, std::forward<K>(key),
                              std::forward<Args>(val)...);
            } else {
                functor(buckets[pos.index].mapped(pos.slot));
            }
            return pos.status == ok;
        }

        template <typename K, typename... Args>
        bool emplace(K&& key, Args&&... val) {
            hash_value hv = hashed_key(key);
            auto b = snapshot_and_lock_two<private_impl::LOCKING_ACTIVE>(hv);
            table_position pos = cuckoo_insert_loop(hv, b, key);
            if (pos.status == ok) {
                add_to_bucket(pos.index, pos.slot, hv.partial,
                              std::forward<K>(key),
                              std::forward<Args>(val)...);
            }

            return pos.status == ok;
        }

        template <typename K, typename... Args>
        bool insert_or_assign(K&& key, Args&&... val)  {
            hash_value hv = hashed_key(key);
            auto b = snapshot_and_lock_two<private_impl::LOCKING_ACTIVE>(hv);
            table_position pos = cuckoo_insert_loop(hv, b, key);
            if (pos.status == ok) {
                add_to_bucket(pos.index, pos.slot, hv.partial, std::forward<K>(key),
                              std::forward<Args>(val)...);
            } else {
                if (std::is_assignable<mapped_type&, mapped_type>::value) {
                    buckets[pos.index].mapped(pos.slot) = mapped_type(std::forward<Args>(val)...);
                } else {
                    buckets.erase_element(pos.index, pos.slot);
                    buckets.set_element(pos.index, pos.slot, hv.partial, std::forward<K>(key),
                                        std::forward<Args>(val)...);
                }
            }
            return pos.status == ok;
        }

        template<typename K, typename... Args>
        size_type update(K&& key, Args&&... val) {
            const hash_value hv = hashed_key(key);
            const auto guard = snapshot_and_lock_two<private_impl::LOCKING_ACTIVE>(hv);
            const table_position pos = cuckoo_find(std::forward<K>(key), hv.partial, guard.first(), guard.second());
            if (pos.status == ok) {
                buckets[pos.index].mapped(pos.slot) = std::forward<mapped_type>(std::forward<Args>(val)...);
                return 1;
            } else {
                return 0;
            }
        }

        template<typename K>
        size_type erase(K&& key) {
            const hash_value hv = hashed_key(key);
            const auto guard = snapshot_and_lock_two<private_impl::LOCKING_ACTIVE>(hv);
            const table_position pos =
            cuckoo_find(std::forward<K>(key), hv.partial, guard.first(), guard.second());
            if (pos.status == ok) {
                del_from_bucket(pos.index, pos.slot);
                return 1;
            } else {
                return 0;
            }
        }

        template<typename K, typename F>
        size_type erase_and_visit(K&& key, F functor) {
            const hash_value hv = hashed_key(key);
            const auto guard = snapshot_and_lock_two<private_impl::LOCKING_ACTIVE>(hv);
            const table_position pos =
                    cuckoo_find(std::forward<K>(key), hv.partial, guard.first(), guard.second());
            if (pos.status == ok) {
                if (functor(buckets[pos.index].mapped(pos.slot))) {
                    del_from_bucket(pos.index, pos.slot);
                }
                return 1;
            } else {
                return 0;
            }
        }

        template<class H2, class P2>
        void merge(concurrent_unordered_map<Key, Value, H2, P2, Allocator>& source) {
            auto locked_table = source.lock_table();
            insert(locked_table.begin(), locked_table.end());
        }
        template<class H2, class P2>
        void merge(concurrent_unordered_map<Key, Value, H2, P2, Allocator>&& source) {
            auto locked_table = source.lock_table();
            insert(locked_table.begin(), locked_table.end());
        }

        void swap(concurrent_unordered_map& other) noexcept {
            std::swap(hash, other.hash);
            std::swap(key_comparator, other.key_comparator);
            buckets.swap(other.buckets);
            all_locks.swap(other.all_locks);

            other.minimum_load_factor_holder.store(
                    minimum_load_factor_holder.exchange(other.minimum_load_factor(), std::memory_order_release),
                    std::memory_order_release);
            other.maximum_hash_power_holder.store(
                    maximum_hash_power_holder.exchange(other.maximum_hashpower(), std::memory_order_release),
                    std::memory_order_release);
        }

        void clear() noexcept {
            auto unlocker = snapshot_and_lock_all<private_impl::LOCKING_ACTIVE>();
            buckets.clear();
            auto& locks = get_current_locks();
            for (size_type i = 0; i < locks.size(); ++i) {
                locks[i].elem_counter() = 0;
            }
        }

    private:
        template <typename U>
        using rebind_alloc =
        typename std::allocator_traits<allocator_type>::template rebind_alloc<U>;

        using locks_t = std::vector<std::private_impl::spinlock, rebind_alloc<std::private_impl::spinlock>>;
        using all_locks_t = std::list<locks_t, rebind_alloc<locks_t>>;

        using hash_value = private_impl::hash_value;

        static constexpr auto SLOTS_PER_BUCKET = private_impl::DEFAULT_SLOTS_PER_BUCKET;
        static constexpr auto MAX_BFS_PATH_LEN = private_impl::MAX_BFS_PATH_LEN;

        using bfs_slot = private_impl::bfs_slot<SLOTS_PER_BUCKET>;
        using bfs_queue = private_impl::bfs_queue<SLOTS_PER_BUCKET>;

        // Hashing types and functions

        // true if the key is small and simple, which means using partial keys for
        // lookup would probably slow us down
        static constexpr bool is_simple =
            std::is_pod<key_type>::value && sizeof(key_type) <= 8;

        // The partial key must only depend on the hash value. It cannot change with
        // the hashpower, because, in order for `cuckoo_fast_double` to work
        // properly, the alt_index must only grow by one bit at the top each time we
        // expand the table.
        static partial_t partial_key(const size_type hash) {
            const uint64_t hash_64bit = hash;
            const uint32_t hash_32bit = (static_cast<uint32_t>(hash_64bit) ^
                                         static_cast<uint32_t>(hash_64bit >> 32));
            const uint16_t hash_16bit = (static_cast<uint16_t>(hash_32bit) ^
                                         static_cast<uint16_t>(hash_32bit >> 16));
            const uint16_t hash_8bit = (static_cast<uint8_t>(hash_16bit) ^
                                        static_cast<uint8_t>(hash_16bit >> 8));
            return hash_8bit;
        }

        template <typename K>
        hash_value hashed_key(const K& key) const {
            const size_type h = hash(key);
            return {h, partial_key(h)};
        }

        template <typename K>
        size_type hashed_key_only_hash(const K &key) const {
            return hash(key);
        }

        // Status codes for internal functions
        enum operation_status {
            ok,
            failure,
            failure_key_not_found,
            failure_key_duplicated,
            failure_table_full,
            failure_under_expansion,
        };

        // A composite type for functions that need to return a table position, and
        // a status code.
        struct table_position {
            size_type index;
            size_type slot;
            operation_status status;
        };

        // hashsize returns the number of buckets corresponding to a given
        // hashpower.
        static inline size_type hashsize(const size_type hashpower) {
            return size_type(1) << hashpower;
        }


        // hashmask returns the bitmask for the buckets array corresponding to a
        // given hashpower.
        static inline size_type hashmask(const size_type hashpower) {
            return hashsize(hashpower) - 1;
        }

        // index_hash returns the first possible bucket that the given hashed key
        // could be.
        static inline size_type index_hash(const size_type hashpower,
                                           const size_type hashvalue) {
            return hashvalue & hashmask(hashpower);
        }

        // alt_index returns the other possible bucket that the given hashed key
        // could be. It takes the first possible bucket as a parameter. Note that
        // this function will return the first possible bucket if index is the
        // second possible bucket, so alt_index(ti, partial, alt_index(ti, partial,
        // index_hash(ti, hv))) == index_hash(ti, hv).
        static inline size_type alt_index(const size_type hashpower, const partial_t partial,
                                          const size_type index) {
            // ensure tag is nonzero for the multiply. 0xc6a4a7935bd1e995 is the
            // hash constant from 64-bit MurmurHash2
            const size_type nonzero_tag = static_cast<size_type>(partial) + 1;
            return (index ^ (nonzero_tag * 0xc6a4a7935bd1e995)) & hashmask(hashpower);
        }

        size_type hashpower() const { return buckets.hashpower(); }

        // try_read_from_bucket will search the bucket for the given key and return
        // the index of the slot if found, or -1 if not found.
        template <typename K>
        int try_read_from_bucket(const bucket& b, const partial_t partial,
                                 const K& key) const {
            // Silence a warning from MSVC about partial being unused if is_simple.
            (void)partial;
            for (size_type i = 0; i < SLOTS_PER_BUCKET; ++i) {
                if (!b.occupied(i) || (!is_simple && partial != b.partial(i))) {
                    continue;
                } else if (key_comparator(b.key(i), key)) {
                    return i;
                }
            }
            return -1;
        }

        template <typename K>
        table_position cuckoo_find(const K& key, const partial_t partial,
                                   const size_type first, const size_type second) const {
            int slot = try_read_from_bucket(buckets[first], partial, key);
            if (slot != -1) {
                return table_position{first, static_cast<size_type>(slot), ok};
            }
            slot = try_read_from_bucket(buckets[second], partial, key);
            if (slot != -1) {
                return table_position{second, static_cast<size_type>(slot), ok};
            }
            return table_position{0, 0, failure_key_not_found};
        }

        // lock_index converts an index into buckets to an index into locks.
        static inline size_type lock_index(const size_type bucket_index) {
            return bucket_index & (std::private_impl::MAX_NUM_LOCKS - 1);
        }

        template <typename LOCK_TYPE>
        class bucket_guard {
        public:
            bucket_guard() {}
            bucket_guard(locks_t* locks, size_type index)
                : locks(locks, unlocker{index})
                {
                }

        private:
            struct unlocker {
                size_type index;
                void operator()(locks_t* p) const {
                    (*p)[lock_index(index)].unlock(LOCK_TYPE());
                }
            };

            std::unique_ptr<locks_t, unlocker> locks;
        };

        template <typename LOCK_TYPE>
        class two_buckets_guard {
        public:
            two_buckets_guard() {}

            two_buckets_guard(locks_t* locks, size_type first, size_type second)
                : locks(locks, two_buckets_unlocker{first, second}) {
            }

            size_type first() const {
                return locks.get_deleter().first;
            }

            size_type second() const {
                return locks.get_deleter().second;
            }

            bool is_active() const {
                return static_cast<bool>(locks);
            }

            void unlock() {
                locks.reset(nullptr);
            }

        private:
            struct two_buckets_unlocker {
                size_type first, second;

                void operator()(locks_t *p) const {
                    const size_type l1 = lock_index(first);
                    const size_type l2 = lock_index(second);
                    (*p)[l1].unlock(LOCK_TYPE());
                    if (l1 != l2) {
                        (*p)[l2].unlock(LOCK_TYPE());
                    }
                }
            };

            std::unique_ptr<locks_t, two_buckets_unlocker> locks;
        };

        template <typename LOCK_TYPE>
        class all_buckets_guard {
        public:
            all_buckets_guard()
                    : all_locks(nullptr)
            {
            }

            all_buckets_guard(all_locks_t* all_locks, typename all_locks_t::iterator first_locked)
                : all_locks(all_locks, unlocker(first_locked))
                {
                }

            bool is_active() const {
                return static_cast<bool>(all_locks);
            }

            void unlock() {
                all_locks.reset(nullptr);
            }

            void release() {
                all_locks.release();
            }

        private:
            struct unlocker {
                typename all_locks_t::iterator first_locked;

                unlocker()
                {
                }

                unlocker(typename all_locks_t::iterator first_locked)
                        : first_locked(first_locked)
                {
                }

                void operator()(all_locks_t* p) const {
                    if (p != nullptr) {
                        for (auto it = first_locked; it != p->end(); ++it) {
                            locks_t& locks = *it;
                            for (std::private_impl::spinlock& lock : locks) {
                                lock.unlock(LOCK_TYPE());
                            }
                        }
                    }
                }
            };

            std::unique_ptr<all_locks_t, unlocker> all_locks;
        };

        // This exception is thrown whenever we try to lock a bucket, but the
        // hashpower is not what was expected
        class hashpower_changed {};

        // After taking a lock on the table for the given bucket, this function will
        // check the hashpower to make sure it is the same as what it was before the
        // lock was taken. If it isn't unlock the bucket and throw a
        // hashpower_changed exception.
        template <typename LOCK_TYPE>
        inline void check_hashpower(const size_type old_hashpower, const size_type lock) const {
            if (hashpower() != old_hashpower) {
                locks_t& locks = get_current_locks();
                locks[lock].unlock(LOCK_TYPE());
                throw hashpower_changed();
            }
        }

        // locks the given bucket index.
        //
        // throws hashpower_changed if it changed after taking the lock.
        template <typename LOCK_TYPE>
        inline bucket_guard<LOCK_TYPE> lock_one(const size_type hashpower,
                                                const size_type index) const {
            const size_type l = lock_index(index);
            locks_t& locks = get_current_locks();
            locks[l].lock(LOCK_TYPE());
            check_hashpower<LOCK_TYPE>(hashpower, l);
            return bucket_guard<LOCK_TYPE>(&locks, index);
        }

        // locks the two bucket indexes, always locking the earlier index first to
        // avoid deadlock. If the two indexes are the same, it just locks one.
        //
        // throws hashpower_changed if it changed after taking the lock.
        template <typename LOCK_TYPE>
        two_buckets_guard<LOCK_TYPE> lock_two(const size_type hashpower, const size_type first,
                                              const size_type second) const {
            size_type l1 = lock_index(first);
            size_type l2 = lock_index(second);
            if (l2 < l1) {
                std::swap(l1, l2);
            }
            locks_t& locks = get_current_locks();

            locks[l1].lock(LOCK_TYPE());
            check_hashpower<LOCK_TYPE>(hashpower, l1);
            if (l2 != l1) {
                locks[l2].lock(LOCK_TYPE());
            }
            return two_buckets_guard<LOCK_TYPE>(&locks, first, second);
        }

        // snapshot_and_lock_all takes all the locks, and returns a deleter object
        // that releases the locks upon destruction. Note that after taking all the
        // locks, it is okay to resize the buckets_ container, since no other threads
        // should be accessing the buckets.
        template <typename LOCK_TYPE>
        all_buckets_guard<LOCK_TYPE> snapshot_and_lock_all() const {
            if (!LOCK_TYPE()) {
                return all_buckets_guard<LOCK_TYPE>();
            }

            // all_locks_ should never decrease in size, so if it is non-empty now, it
            // will remain non-empty
            assert(!all_locks.empty());
            auto first_locked = all_locks.end();
            --first_locked;
            auto current_locks = first_locked;
            while (current_locks != all_locks.end()) {
                locks_t& locks = *current_locks;
                for (std::private_impl::spinlock& lock : locks) {
                    lock.lock(LOCK_TYPE());
                }
                ++current_locks;
            }
            // Once we have taken all the locks of the "current" container, nobody
            // else can do locking operations on the table.
            return all_buckets_guard<LOCK_TYPE>(&all_locks, first_locked);
        }

        template <typename LOCK_TYPE>
        std::pair<two_buckets_guard<LOCK_TYPE>, bucket_guard<LOCK_TYPE>>
            lock_three(const size_type hp, const size_type i1, const size_type i2,
                       const size_type i3) const {
            std::array<size_type, 3> l{{lock_index(i1), lock_index(i2), lock_index(i3)}};
            // Lock in order.
            if (l[2] < l[1])
                std::swap(l[2], l[1]);
            if (l[2] < l[0])
                std::swap(l[2], l[0]);
            if (l[1] < l[0])
                std::swap(l[1], l[0]);
            locks_t& locks = get_current_locks();
            locks[l[0]].lock(LOCK_TYPE());
            check_hashpower<LOCK_TYPE>(hp, l[0]);
            if (l[1] != l[0]) {
                locks[l[1]].lock(LOCK_TYPE());
            }
            if (l[2] != l[1]) {
                locks[l[2]].lock(LOCK_TYPE());
            }
            return std::make_pair(two_buckets_guard<LOCK_TYPE>(&locks, i1, i2),
                                  bucket_guard<LOCK_TYPE>((lock_index(i3) == lock_index(i1) ||
                                                           lock_index(i3) == lock_index(i2))
                                                          ? nullptr
                                                          : &locks,
                                                          i3));
        }

        // snapshot_and_lock_two loads locks the buckets associated with the given
        // hash value, making sure the hashpower doesn't change before the locks are
        // taken. Thus it ensures that the buckets and locks corresponding to the
        // hash value will stay correct as long as the locks are held. It returns
        // the bucket indices associated with the hash value and the current
        // hashpower.
        template <typename LOCK_TYPE>
        two_buckets_guard<LOCK_TYPE> snapshot_and_lock_two(const hash_value& hashvalue) const {
            while (true) {
                // Store the current hashpower we're using to compute the buckets
                const size_type old_hashpower = hashpower();
                const size_type first = index_hash(old_hashpower, hashvalue.hash);
                const size_type second = alt_index(old_hashpower, hashvalue.partial, first);
                try {
                    return lock_two<LOCK_TYPE>(old_hashpower, first, second);
                } catch (hashpower_changed &) {
                    // The hashpower changed while taking the locks. Try again.
                    continue;
                }
            }
        }

        template <typename K, typename LOCK_TYPE>
        table_position cuckoo_insert_loop(hash_value hashvalue,
                                          two_buckets_guard<LOCK_TYPE>& guard,
                                          K& key) {
            table_position pos;
            while (true) {
                assert(guard.is_active());
                const size_type old_hashpower = hashpower();
                pos = cuckoo_insert(hashvalue, guard, key);
                switch (pos.status) {
                case ok:
                case failure_key_duplicated:
                    return pos;
                case failure_table_full:
                    // Expand the table and try again, re-grabbing the locks
                    cuckoo_fast_double<LOCK_TYPE, automatic_resize>(old_hashpower);
                    guard = snapshot_and_lock_two<LOCK_TYPE>(hashvalue);
                    break;
                case failure_under_expansion:
                    // The table was under expansion while we were cuckooing. Re-grab the
                    // locks and try again.
                    guard = snapshot_and_lock_two<LOCK_TYPE>(hashvalue);
                    break;
                default:
                    assert(false);
                }
            }
        }

        // run_cuckoo performs cuckoo hashing on the table in an attempt to free up
        // a slot on either of the insert buckets, which are assumed to be locked
        // before the start. On success, the bucket and slot that was freed up is
        // stored in insert_bucket and insert_slot. In order to perform the search
        // and the swaps, it has to release the locks, which can lead to certain
        // concurrency issues, the details of which are explained in the function.
        // If run_cuckoo returns ok (success), then `b` will be active, otherwise it
        // will not.
        template <typename LOCK_TYPE>
        operation_status run_cuckoo(two_buckets_guard<LOCK_TYPE>& guard,
                                    size_type& insert_bucket,
                                    size_type& insert_slot) {
            // We must unlock the buckets here, so that cuckoopath_search and
            // cuckoopath_move can lock buckets as desired without deadlock.
            // cuckoopath_move has to move something out of one of the original
            // buckets as its last operation, and it will lock both buckets and
            // leave them locked after finishing. This way, we know that if
            // cuckoopath_move succeeds, then the buckets needed for insertion are
            // still locked. If cuckoopath_move fails, the buckets are unlocked and
            // we try again. This unlocking does present two problems. The first is
            // that another insert on the same key runs and, finding that the key
            // isn't in the table, inserts the key into the table. Then we insert
            // the key into the table, causing a duplication. To check for this, we
            // search the buckets for the key we are trying to insert before doing
            // so (this is done in cuckoo_insert, and requires that both buckets are
            // locked). Another problem is that an expansion runs and changes the
            // hashpower, meaning the buckets may not be valid anymore. In this
            // case, the cuckoopath functions will have thrown a hashpower_changed
            // exception, which we catch and handle here.
            size_type hp = hashpower();
            guard.unlock();
            private_impl::nodes path;
            bool done = false;
            try {
                while (!done) {
                    const int depth =
                        cuckoopath_search<LOCK_TYPE>(hp, path, guard.first(), guard.second());
                    if (depth < 0) {
                        break;
                    }

                    if (cuckoopath_move(hp, path, depth, guard)) {
                        insert_bucket = path[0].bucket;
                        insert_slot = path[0].slot;
                        assert(insert_bucket == guard.first() || insert_bucket == guard.second());
                        assert(LOCK_TYPE() == private_impl::LOCKING_INACTIVE() ||
                               !get_current_locks()[lock_index(guard.first())].try_lock(LOCK_TYPE()));
                        assert(LOCK_TYPE() == private_impl::LOCKING_INACTIVE() ||
                               !get_current_locks()[lock_index(guard.second())].try_lock(LOCK_TYPE()));
                        assert(!buckets[insert_bucket].occupied(insert_slot));
                        done = true;
                        break;
                    }
                }
            } catch (hashpower_changed &) {
                // The hashpower changed while we were trying to cuckoo, which means
                // we want to retry. b.first() and b.second() should not be locked
                // in this case.
                return failure_under_expansion;
            }
            return done ? ok : failure;
        }

        // slot_search searches for a cuckoo path using breadth-first search. It
        // starts with the i1 and i2 buckets, and, until it finds a bucket with an
        // empty slot, adds each slot of the bucket in the bfs_slot. If the queue runs
        // out of space, it fails.
        //
        // throws hashpower_changed if it changed during the search
        template <typename LOCK_TYPE>
        bfs_slot slot_search(const size_type hp, const size_type i1,
                             const size_type i2) {
            bfs_queue q;
            // The initial pathcode informs cuckoopath_search which bucket the path
            // starts on
            q.enqueue(bfs_slot(i1, 0, 0));
            q.enqueue(bfs_slot(i2, 1, 0));
            while (!q.full() && !q.empty()) {
                bfs_slot x = q.dequeue();
                // Picks a (sort-of) random slot to start from
                size_type starting_slot = x.pathcode % SLOTS_PER_BUCKET;
                for (size_type i = 0; i < SLOTS_PER_BUCKET && !q.full(); ++i) {
                    size_type slot = (starting_slot + i) % SLOTS_PER_BUCKET;
                    auto ob = lock_one<LOCK_TYPE>(hp, x.bucket);
                    bucket& b = buckets[x.bucket];
                    if (!b.occupied(slot)) {
                        // We can terminate the search here
                        x.pathcode = x.pathcode * SLOTS_PER_BUCKET + slot;
                        return x;
                    }

                    // If x has less than the maximum number of path components,
                    // create a new bfs_slot item, that represents the bucket we would
                    // have come from if we kicked out the item at this slot.
                    const partial_t partial = b.partial(slot);
                    if (x.depth < MAX_BFS_PATH_LEN - 1) {
                        bfs_slot y(alt_index(hp, partial, x.bucket),
                                 x.pathcode * SLOTS_PER_BUCKET + slot, x.depth + 1);
                        q.enqueue(y);
                    }
                }
            }
            // We didn't find a short-enough cuckoo path, so the queue ran out of
            // space. Return a failure value.
            return bfs_slot(0, 0, -1);
        }

        // cuckoopath_search finds a cuckoo path from one of the starting buckets to
        // an empty slot in another bucket. It returns the depth of the discovered
        // cuckoo path on success, and -1 on failure. Since it doesn't take locks on
        // the buckets it searches, the data can change between this function and
        // cuckoopath_move. Thus cuckoopath_move checks that the data matches the
        // cuckoo path before changing it.
        //
        // throws hashpower_changed if it changed during the search.
        template <typename LOCK_TYPE>
        int cuckoopath_search(const size_type hp, private_impl::nodes& path,
                              const size_type i1, const size_type i2) {
            private_impl::bfs_slot<SLOTS_PER_BUCKET> compressed_path =
            slot_search<LOCK_TYPE>(hp, i1, i2);
            if (compressed_path.depth == -1) {
                return -1;
            }
            // Fill in the cuckoo path slots from the end to the beginning.
            for (int i = compressed_path.depth; i >= 0; i--) {
                path[i].slot = compressed_path.pathcode % SLOTS_PER_BUCKET;
                compressed_path.pathcode /= SLOTS_PER_BUCKET;
            }
            // Fill in the cuckoo_path buckets and keys from the beginning to the
            // end, using the final pathcode to figure out which bucket the path
            // starts on. Since data could have been modified between slot_search
            // and the computation of the cuckoo path, this could be an invalid
            // cuckoo_path.
            private_impl::node& first = path[0];
            if (compressed_path.pathcode == 0) {
                first.bucket = i1;
            } else {
                assert(compressed_path.pathcode == 1);
                first.bucket = i2;
            }
            {
                const auto guard = lock_one<LOCK_TYPE>(hp, first.bucket);
                const bucket& b = buckets[first.bucket];
                if (!b.occupied(first.slot)) {
                    // We can terminate here
                    return 0;
                }
                first.hv = hashed_key(b.key(first.slot));
            }
            for (int i = 1; i <= compressed_path.depth; ++i) {
                private_impl::node& cur = path[i];
                const private_impl::node& prev = path[i - 1];
                assert(prev.bucket == index_hash(hp, prev.hv.hash) ||
                       prev.bucket ==
                       alt_index(hp, prev.hv.partial, index_hash(hp, prev.hv.hash)));
                // We get the bucket that this slot is on by computing the alternate
                // index of the previous bucket
                cur.bucket = alt_index(hp, prev.hv.partial, prev.bucket);
                const auto guard = lock_one<LOCK_TYPE>(hp, cur.bucket);
                const bucket& b = buckets[cur.bucket];
                if (!b.occupied(cur.slot)) {
                    // We can terminate here
                    return i;
                }
                cur.hv = hashed_key(b.key(cur.slot));
            }
            return compressed_path.depth;
        }

        // Checks whether the resize is okay to proceed. Returns a status code, or
        // throws an exception, depending on the error type.
        using automatic_resize = std::integral_constant<bool, true>;
        using manual_resize = std::integral_constant<bool, false>;

        template <typename AUTO_RESIZE>
        operation_status check_resize_validity(const size_type orig_hp,
                                               const size_type new_hp) {
            const size_type mhp = maximum_hash_power_holder.load(std::memory_order_acquire);
            if (mhp != private_impl::NO_MAXIMUM_HASHPOWER && new_hp > mhp) {
                throw private_impl::maximum_hashpower_exceeded(new_hp);
            }
            if (AUTO_RESIZE::value && load_factor() < minimum_load_factor()) {
                throw private_impl::load_factor_too_low(minimum_load_factor());
            }
            if (hashpower() != orig_hp) {
                // Most likely another expansion ran before this one could grab the
                // locks
                return failure_under_expansion;
            }
            return ok;
        }

        // When we expand the contanier, we may need to expand the locks array, if
        // the current locks array is smaller than the maximum size and also smaller
        // than the number of buckets in the upcoming buckets container. In this
        // case, we grow the locks array to the smaller of the maximum lock array
        // size and the bucket count. This is done by allocating an entirely new lock
        // container, taking all the locks, copying over the counters, and then
        // finally adding it to the end of `all_locks_`, thereby designating it the
        // "current" locks container. It is the responsibility of the caller to
        // unlock all locks taken, including the new locks, whenever it is done with
        // them, so that old threads can resume and potentially re-start.
        template <typename LOCK_TYPE>
        void maybe_resize_locks(size_type new_bucket_count) {
            locks_t& current_locks = get_current_locks();
            if (!(current_locks.size() < std::private_impl::MAX_NUM_LOCKS &&
                  current_locks.size() < new_bucket_count)) {
                return;
            }

            locks_t new_locks(std::min(size_type(std::private_impl::MAX_NUM_LOCKS), new_bucket_count),
                              std::private_impl::spinlock(), get_allocator());
            for (std::private_impl::spinlock& lock : new_locks) {
                lock.lock(LOCK_TYPE());
            }
            assert(new_locks.size() > current_locks.size());
            std::copy(current_locks.begin(), current_locks.end(), new_locks.begin());
            all_locks.emplace_back(std::move(new_locks));
        }

        // cuckoo_fast_double will double the size of the table by taking advantage
        // of the properties of index_hash and alt_index. If the key's move
        // constructor is not noexcept, we use cuckoo_expand_simple, since that
        // provides a strong exception guarantee.
        template <typename LOCK_TYPE, typename AUTO_RESIZE>
        operation_status cuckoo_fast_double(size_type current_hp) {
            if (!std::is_nothrow_move_constructible<key_type>::value ||
                !std::is_nothrow_move_constructible<mapped_type>::value) {
                return cuckoo_expand_simple<LOCK_TYPE, AUTO_RESIZE>(current_hp + 1);
            }
            const size_type new_hp = current_hp + 1;
            auto unlocker = snapshot_and_lock_all<LOCK_TYPE>();

            auto st = check_resize_validity<AUTO_RESIZE>(current_hp, new_hp);
            if (st != ok) {
                return st;
            }

            buckets_t new_buckets(new_hp, get_allocator());

            // We gradually unlock the new table, by processing each of the buckets
            // corresponding to each lock we took. For each slot in an old bucket,
            // we either leave it in the old bucket, or move it to the corresponding
            // new bucket. After we're done with the bucket, we release the lock on
            // it and the new bucket, letting other threads using the new map
            // gradually. We only unlock the locks being used by the old table,
            // because unlocking new locks would enable operations on the table
            // before we want them. We also re-evaluate the partial key stored at
            // each slot, since it depends on the hashpower.
            parallel_exec(0, hashsize(current_hp),
                          [this, current_hp, new_hp, &new_buckets](size_type start, size_type end,
                                                     std::exception_ptr &eptr) {
                              try {
                                  move_buckets<LOCK_TYPE>(new_buckets, current_hp, new_hp, start, end);
                              } catch (...) {
                                  eptr = std::current_exception();
                              }
                          });

            maybe_resize_locks<LOCK_TYPE>(1UL << new_hp);
            buckets.swap(new_buckets);
            return ok;
        }

        template <typename LOCK_TYPE>
        void move_buckets(buckets_t& new_buckets, size_type current_hp, size_type new_hp,
                          size_type start_lock_ind, size_type end_lock_ind) {
            for (size_type old_bucket_ind = start_lock_ind; old_bucket_ind < end_lock_ind;
                 ++old_bucket_ind) {
                // By doubling the table size, the index_hash and alt_index of
                // each key got one bit added to the top, at position
                // current_hp, which means anything we have to move will either
                // be at the same bucket position, or exactly
                // hashsize(current_hp) later than the current bucket
                bucket &old_bucket = buckets[old_bucket_ind];
                const size_type new_bucket_ind = old_bucket_ind + hashsize(current_hp);
                size_type new_bucket_slot = 0;

                // For each occupied slot, either move it into its same position in the
                // new buckets container, or to the first available spot in the new
                // bucket in the new buckets container.
                for (size_type old_bucket_slot = 0; old_bucket_slot < std::private_impl::DEFAULT_SLOTS_PER_BUCKET;
                     ++old_bucket_slot) {
                    if (!old_bucket.occupied(old_bucket_slot)) {
                        continue;
                    }
                    const hash_value hv = hashed_key(old_bucket.key(old_bucket_slot));
                    const size_type old_ihash = index_hash(current_hp, hv.hash);
                    const size_type old_ahash =
                            alt_index(current_hp, hv.partial, old_ihash);
                    const size_type new_ihash = index_hash(new_hp, hv.hash);
                    const size_type new_ahash = alt_index(new_hp, hv.partial, new_ihash);
                    size_type dst_bucket_ind, dst_bucket_slot;
                    if ((old_bucket_ind == old_ihash && new_ihash == new_bucket_ind) ||
                        (old_bucket_ind == old_ahash && new_ahash == new_bucket_ind)) {
                        // We're moving the key to the new bucket
                        dst_bucket_ind = new_bucket_ind;
                        dst_bucket_slot = new_bucket_slot++;
                    } else {
                        // We're moving the key to the old bucket
                        assert((old_bucket_ind == old_ihash && new_ihash == old_ihash) ||
                               (old_bucket_ind == old_ahash && new_ahash == old_ahash));
                        dst_bucket_ind = old_bucket_ind;
                        dst_bucket_slot = old_bucket_slot;
                    }
                    new_buckets.set_element(dst_bucket_ind, dst_bucket_slot++,
                                            old_bucket.partial(old_bucket_slot),
                                            old_bucket.movable_key(old_bucket_slot),
                                            std::move(old_bucket.mapped(old_bucket_slot)));
                }
            }
        }

        static size_type reserve_calc(const size_type n) {
            const size_type buckets = (n + SLOTS_PER_BUCKET - 1) / SLOTS_PER_BUCKET;
            size_type blog2;
            for (blog2 = 0; (1UL << blog2) < buckets; ++blog2)
                ;
            assert(n <= buckets * SLOTS_PER_BUCKET && buckets <= hashsize(blog2));
            return blog2;
        }

        locks_t& get_current_locks() const {
            return all_locks.back();
        }

        // move_element moves a key-value pair from one location to another, also updating
        // the lock counters. Assumes locks are already taken.
        void move_element(size_type dst_bucket, size_type dst_slot, size_type src_bucket,
                          size_type src_slot) {
            buckets.move_element(dst_bucket, dst_slot, src_bucket, src_slot);
            --get_current_locks()[lock_index(src_bucket)].elem_counter();
            ++get_current_locks()[lock_index(dst_bucket)].elem_counter();
        }


        // cuckoo_expand_simple will resize the table to at least the given
        // new_hashpower. When we're shrinking the table, if the current table
        // contains more elements than can be held by new_hashpower, the resulting
        // hashpower will be greater than new_hashpower. It needs to take all the
        // bucket locks, since no other operations can change the table during
        // expansion. Throws libcuckoo_maximum_hashpower_exceeded if we're expanding
        // beyond the maximum hashpower, and we have an actual limit.
        template <typename LOCK_TYPE, typename AUTO_RESIZE>
        operation_status cuckoo_expand_simple(size_type new_hp) {
            const auto unlocker = snapshot_and_lock_all<LOCK_TYPE>();
            const size_type hp = hashpower();
            auto st = check_resize_validity<AUTO_RESIZE>(hp, new_hp);
            if (st != ok) {
                return st;
            }
            // Creates a new hash table with hashpower new_hp and adds all
            // the elements from the old buckets.
            concurrent_unordered_map new_map(hashsize(new_hp) * SLOTS_PER_BUCKET,
                                             hash_function(), key_eq(), get_allocator());

            parallel_exec(0, hashsize(hp), [this, &new_map](size_type i, size_type end,
                                                            std::exception_ptr &eptr) {
                              try {
                                  for (; i < end; ++i) {
                                      for (size_type j = 0; j < SLOTS_PER_BUCKET; ++j) {
                                          if (buckets[i].occupied(j)) {
                                              new_map.emplace(buckets[i].movable_key(j),
                                                             std::move(buckets[i].mapped(j)));
                                          }
                                      }
                                  }
                              } catch (...) {
                                  eptr = std::current_exception();
                              }
                          });

            // Swap the current buckets containers with new_map's. This is okay,
            // because we have all the locks, so nobody else should be reading from the
            // buckets array. Then the old buckets array will be deleted when new_map
            // is deleted. We also need to `emulate` new_map's locks array, so we have
            // the same size and data content. We can't swap the memory though, since
            // other threads may still be looking at our lock memory. Regardless, we
            // shouldn't need to swap the memory, since new_map is using the same
            // allocator as we are.
            buckets.swap(new_map.buckets);
            all_locks.swap(new_map.all_locks);
            return ok;
        }

        // cuckoopath_move moves keys along the given cuckoo path in order to make
        // an empty slot in one of the buckets in cuckoo_insert. Before the start of
        // this function, the two insert-locked buckets were unlocked in run_cuckoo.
        // At the end of the function, if the function returns true (success), then
        // both insert-locked buckets remain locked. If the function is
        // unsuccessful, then both insert-locked buckets will be unlocked.
        //
        // throws hashpower_changed if it changed during the move.
        template <typename LOCK_TYPE>
        bool cuckoopath_move(const size_type hp, private_impl::nodes& path,
                             size_type depth, two_buckets_guard<LOCK_TYPE>& guard) {
            assert(!guard.is_active());
            if (depth == 0) {
                // There is a chance that depth == 0, when try_add_to_bucket sees
                // both buckets as full and cuckoopath_search finds one empty. In
                // this case, we lock both buckets. If the slot that
                // cuckoopath_search found empty isn't empty anymore, we unlock them
                // and return false. Otherwise, the bucket is empty and insertable,
                // so we hold the locks and return true.
                const size_type bucket = path[0].bucket;
                assert(bucket == guard.first() || bucket == guard.second());
                guard = lock_two<LOCK_TYPE>(hp, guard.first(), guard.second());
                if (!buckets[bucket].occupied(path[0].slot)) {
                    return true;
                } else {
                    guard.unlock();
                    return false;
                }
            }

            while (depth > 0) {
                private_impl::node& from = path[depth - 1];
                private_impl::node& to = path[depth];
                const size_type from_slot = from.slot;
                const size_type to_slot = to.slot;
                two_buckets_guard<LOCK_TYPE> twob;
                bucket_guard<LOCK_TYPE> extrab;
                if (depth == 1) {
                    // Even though we are only swapping out of one of the original
                    // buckets, we have to lock both of them along with the slot we
                    // are swapping to, since at the end of this function, they both
                    // must be locked. We store tb inside the extrab container so it
                    // is unlocked at the end of the loop.
                    std::tie(twob, extrab) =
                        lock_three<LOCK_TYPE>(hp, guard.first(), guard.second(), to.bucket);
                } else {
                    twob = lock_two<LOCK_TYPE>(hp, from.bucket, to.bucket);
                }

                bucket& from_bucket = buckets[from.bucket];
                bucket& to_bucket = buckets[to.bucket];

                // We plan to kick out fs, but let's check if it is still there;
                // there's a small chance we've gotten scooped by a later cuckoo. If
                // that happened, just... try again. Also the slot we are filling in
                // may have already been filled in by another thread, or the slot we
                // are moving from may be empty, both of which invalidate the swap.
                // We only need to check that the hash value is the same, because,
                // even if the keys are different and have the same hash value, then
                // the cuckoopath is still valid.
                if (hashed_key_only_hash(from_bucket.key(from_slot)) != from.hv.hash ||
                    to_bucket.occupied(to_slot) ||
                    !from_bucket.occupied(from_slot)) {
                    return false;
                }

                move_element(to.bucket, to_slot, from.bucket, from_slot);
                if (depth == 1) {
                    // Hold onto the locks contained in twob
                    guard = std::move(twob);
                }
                depth--;
            }
            return true;
        }


        // add_to_bucket will insert the given key-value pair into the slot. The key
        // and value will be move-constructed into the table, so they are not valid
        // for use afterwards.
        template <typename K, typename... Args>
        void add_to_bucket(const size_type bucket_index, const size_type slot,
                           const partial_t partial, K&& key, Args&&... val) {
            buckets.set_element(bucket_index, slot, partial,
                                std::forward<K>(key),
                                std::forward<Args>(val)...);
            ++get_current_locks()[lock_index(bucket_index)].elem_counter();
        }

        template <typename K>
        bool try_find_insert_bucket(const bucket& b, int& slot,
                                    const partial_t partial, const K& key) const {
            // Silence a warning from MSVC about partial being unused if is_simple.
            (void)partial;
            slot = -1;
            for (size_type i = 0; i < SLOTS_PER_BUCKET; ++i) {
                if (b.occupied(i)) {
                    if (!is_simple && partial != b.partial(i)) {
                        continue;
                    }
                    if (key_comparator(b.key(i), key)) {
                        slot = i;
                        return false;
                    }
                } else {
                    slot = i;
                }
            }
            return true;
        }

        template <typename K, typename LOCK_TYPE>
        table_position cuckoo_insert(const hash_value hashvalue,
                                     two_buckets_guard<LOCK_TYPE>& guard,
                                     K& key) {
            int res1, res2;
            bucket& b1 = buckets[guard.first()];
            if (!try_find_insert_bucket(b1, res1, hashvalue.partial, key)) {
                return table_position{guard.first(), static_cast<size_type>(res1),
                        failure_key_duplicated};
            }
            bucket& b2 = buckets[guard.second()];
            if (!try_find_insert_bucket(b2, res2, hashvalue.partial, key)) {
                return table_position{guard.second(), static_cast<size_type>(res2),
                        failure_key_duplicated};
            }
            if (res1 != -1) {
                return table_position{guard.first(), static_cast<size_type>(res1), ok};
            }
            if (res2 != -1) {
                return table_position{guard.second(), static_cast<size_type>(res2), ok};
            }

            // We are unlucky, so let's perform cuckoo hashing.
            size_type insert_bucket = 0;
            size_type insert_slot = 0;
            auto st = run_cuckoo<LOCK_TYPE>(guard, insert_bucket, insert_slot);
            if (st == failure_under_expansion) {
                // The run_cuckoo operation operated on an old version of the table,
                // so we have to try again. We signal to the calling insert method
                // to try again by returning failure_under_expansion.
                return table_position{0, 0, failure_under_expansion};
            } else if (st == ok) {
                assert(LOCK_TYPE() == private_impl::LOCKING_INACTIVE() ||
                       !get_current_locks()[lock_index(guard.first())].try_lock(LOCK_TYPE()));
                assert(LOCK_TYPE() == private_impl::LOCKING_INACTIVE() ||
                       !get_current_locks()[lock_index(guard.second())].try_lock(LOCK_TYPE()));
                assert(!buckets[insert_bucket].occupied(insert_slot));
                assert(insert_bucket == index_hash(hashpower(), hashvalue.hash) ||
                       insert_bucket == alt_index(hashpower(), hashvalue.partial,
                                                  index_hash(hashpower(), hashvalue.hash)));
                // Since we unlocked the buckets during run_cuckoo, another insert
                // could have inserted the same key into either b.first() or
                // b.second(), so we check for that before doing the insert.
                table_position pos = cuckoo_find(key, hashvalue.partial,
                                                 guard.first(), guard.second());
                if (pos.status == ok) {
                    pos.status = failure_key_duplicated;
                    return pos;
                }
                return table_position{insert_bucket, insert_slot, ok};
            }
            assert(st == failure);
            return table_position{0, 0, failure_table_full};
        }

        template <typename F>
        static void parallel_exec(size_type start, size_type end, F func) {
            static const size_type num_threads =
            std::max(std::thread::hardware_concurrency(), 1U);
            size_type work_per_thread = (end - start) / num_threads;
            std::vector<std::thread> threads(num_threads);
            std::vector<std::exception_ptr> eptrs(num_threads, nullptr);
            for (size_type i = 0; i < num_threads - 1; ++i) {
                threads[i] =
                    std::thread(func, start, start + work_per_thread, std::ref(eptrs[i]));
                start += work_per_thread;
            }
            threads.back() = std::thread(func, start, end, std::ref(eptrs.back()));
            for (std::thread &t : threads) {
                t.join();
            }
            for (std::exception_ptr &eptr : eptrs) {
                if (eptr) {
                    std::rethrow_exception(eptr);
                }
            }
        }

        void del_from_bucket(const size_type bucket_index, const size_type slot) {
            buckets.erase_element(bucket_index, slot);
            assert(get_current_locks()[lock_index(bucket_index)].elem_counter() > 0);
            --get_current_locks()[lock_index(bucket_index)].elem_counter();
        }

        void minimum_load_factor(const double mlf) {
            if (mlf < 0.0) {
                throw std::invalid_argument("load factor " + std::to_string(mlf) +
                                            " cannot be "
                                            "less than 0");
            } else if (mlf > 1.0) {
                throw std::invalid_argument("load factor " + std::to_string(mlf) +
                                            " cannot be "
                                            "greater than 1");
            }
            minimum_load_factor_holder.store(mlf, std::memory_order_release);
        }
        double minimum_load_factor() {
            return minimum_load_factor_holder.load(std::memory_order_acquire);
        }

        void maximum_hashpower(size_type mhp) {
            if (hashpower() > mhp) {
                throw std::invalid_argument("maximum hashpower " + std::to_string(mhp) +
                                            " is less than current hashpower");
            }
            maximum_hash_power_holder.store(mhp, std::memory_order_release);
        }
        size_type maximum_hashpower() {
            return maximum_hash_power_holder.load(std::memory_order_acquire);
        }

        double load_factor() const {
            return static_cast<double>(size()) / static_cast<double>(capacity());
        }

        size_type size() const {
            size_type s = 0;
            if (!all_locks.empty()) {
                auto &locks = get_current_locks();
                for (size_type i = 0; i < locks.size(); ++i) {
                    s += locks[i].elem_counter();
                }
            }
            return s;
        }
        size_type capacity() const {
            return bucket_count() * SLOTS_PER_BUCKET;
        }

        size_type bucket_count() const {
            return 1UL << hashpower();
        }

    private:
        allocator_type allocator;
        hasher hash;
        key_equal key_comparator;
        buckets_t buckets;
        mutable all_locks_t all_locks;

        std::atomic<size_type> minimum_load_factor_holder;
        std::atomic<size_type> maximum_hash_power_holder;

        friend unit_test_internals_view;
    };
}
