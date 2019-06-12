#include <iostream>
#include <string>
#include <unordered_map>
#include <random>
#include <thread>
#include <mutex>
#include <vector>
#include <limits>
#include <cassert>
#include <functional>

#include <libcuckoo/cuckoohash_map.hh>

#include <boost/thread/synchronized_value.hpp>
#include <boost/iterator/indirect_iterator.hpp>
#include <folly/concurrency/ConcurrentHashMap.h>

#include <concurrent_hash_map/concurrent_hash_map.hpp>

template<
        class Key,
        class Value,
        class Hash = std::hash<Key>,
        class KeyEqual = std::equal_to<Key>,
        class Allocator = std::allocator<std::pair<const Key, Value>>,
        class Locable = std::mutex
>
class concurrent_unordered_map {
public:
    using bucket_type = std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>;
    using allocator_type = typename bucket_type::allocator_type;
    using key_type = typename bucket_type::key_type;
    using mapped_type = typename bucket_type::mapped_type;
    using value_type = typename bucket_type::value_type;
    using size_type = typename bucket_type::size_type;
    using difference_type = typename bucket_type::difference_type;
    using hasher = typename bucket_type::hasher;
    using key_equal = typename bucket_type::key_equal;
    using reference = typename bucket_type::reference;
    using const_reference = typename bucket_type::const_reference;
    using pointer = typename bucket_type::pointer;
    using const_pointer = typename bucket_type::const_pointer;
    using local_iterator = typename bucket_type::iterator;
    using const_local_iterator = typename bucket_type::const_iterator;

private:
    using bucket_allocator = typename std::allocator_traits<allocator_type>::template rebind_alloc<bucket_type>;
    using locable_allocator = typename std::allocator_traits<allocator_type>::template rebind_alloc<Locable>;

    hasher hash;
    key_equal equal;
    allocator_type allocator;
    std::vector<bucket_type, bucket_allocator> buckets;
    mutable std::vector<Locable, locable_allocator> bucket_mutexes;

public:
    static constexpr size_type DEFAULT_BUCKET_COUNT = 16;

    explicit concurrent_unordered_map(size_type bucket_count = DEFAULT_BUCKET_COUNT,
                                      const Hash& hash = Hash(),
                                      const KeyEqual& equal = KeyEqual(),
                                      const allocator_type& allocator = allocator_type()) :
            hash(hash),
            equal(equal),
            allocator(allocator),
            buckets(bucket_allocator()),
            bucket_mutexes(bucket_count, locable_allocator())
    {
        assert(bucket_count > 0);
        for (size_type i = 0; i < bucket_count; ++i) {
            bucket_type bucket(bucket_count, hash, equal, allocator_type());
            buckets.push_back(std::move(bucket));
        }
    }

    explicit concurrent_unordered_map(const allocator_type& allocator) :
            concurrent_unordered_map(DEFAULT_BUCKET_COUNT, Hash(), KeyEqual(), allocator)
    {
    }
    template< class InputIt >
    concurrent_unordered_map(InputIt first, InputIt last,
                             size_type bucket_count = DEFAULT_BUCKET_COUNT,
                             const Hash& hash = Hash(),
                             const KeyEqual& equal = KeyEqual(),
                             const allocator_type& allocator = allocator_type()) :
            concurrent_unordered_map(bucket_count, hash, equal, allocator)
    {
        insert(first, last);
    }

    concurrent_unordered_map(std::initializer_list<value_type> init,
                             size_type bucket_count = DEFAULT_BUCKET_COUNT,
                             const Hash& hash = Hash(),
                             const KeyEqual& equal = KeyEqual(),
                             const allocator_type& allocator = allocator_type()) :
            concurrent_unordered_map(bucket_count, hash, equal, allocator)
    {
        insert(init.begin(), init.end());
    }

    concurrent_unordered_map(const concurrent_unordered_map&) = delete;

    concurrent_unordered_map(concurrent_unordered_map&& other) {
        swap(other);
    }
    allocator_type get_allocator() const {
        return allocator;
    }

    concurrent_unordered_map& operator=(const concurrent_unordered_map&) = delete;
    concurrent_unordered_map& operator=(std::initializer_list<value_type>) = delete;

    concurrent_unordered_map& operator=(concurrent_unordered_map&& other) {
        swap(other);
        return *this;
    }
    bool empty() const {
        lock_all();
        try {
            bool result = true;
            for (int i = 0; i < buckets.size(); ++i) {
                if (!buckets[i].empty()) {
                    result = false;
                    break;
                }
            }
            unlock_all();
            return result;
        } catch(...) {
            unlock_all();
            throw;
        }
    }

    size_type size() const {
        lock_all();
        try {
            size_type result = 0;
            for (size_type i = 0; i < buckets.size(); ++i) {
                result += buckets[i].size();
            }
            unlock_all();
            return result;
        } catch(...) {
            unlock_all();
            throw;
        }
    }


    void clear() {
        lock_all();
        try {
            for (size_type i = 0; i < buckets.size(); ++i) {
                buckets[i].clear();
            }
            unlock_all();
        } catch(...) {
            unlock_all();
            throw;
        }
    }

    size_type max_size() const {
        return buckets[0].max_size() / buckets.size();
    }

    template<typename T>
    class reference_guard : public std::reference_wrapper<T> {
    public:
        explicit reference_guard(T& t) noexcept : std::reference_wrapper<T>(t) {
        }
    };

    reference_guard<const Value> operator[](const Key& key) const {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        return reference_guard<const Value>(buckets[bucket_id][key]);
    }

    reference_guard<Value> operator[](const Key& key) {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        return reference_guard<Value>(buckets[bucket_id][key]);
    }

    reference_guard<Value> at(const Key& key) {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        return reference_guard<Value>(buckets[bucket_id].at(key));
    }

    reference_guard<const Value> at(const Key& key) const {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        return reference_guard<const Value>(buckets[bucket_id].at(key));
    }

    size_type count(const Key& key) const {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        return buckets[bucket_id].count(key);
    }

    void swap(concurrent_unordered_map<Key, Value, Hash, KeyEqual, Allocator>& other) {
        using mutex_list = std::vector<Locable*>;
        mutex_list mutexes;

        for (size_type i = 0; i < buckets.size(); ++i) {
            mutexes.push_back(&bucket_mutexes[i]);
        }

        for (size_type i = 0; i < other.buckets.size(); ++i) {
            mutexes.push_back(&other.bucket_mutexes[i]);
        }

        boost::indirect_iterator<typename mutex_list::iterator> first(mutexes.begin());
        boost::indirect_iterator<typename mutex_list::iterator> last(mutexes.end());
        boost::lock(first, last);

        try {
            this->bucket_mutexes.swap(other.bucket_mutexes);
            this->buckets.swap(other.buckets);
            std::swap(this->hash, other.hash);
        } catch(...) {
            unlock(first, last);
            throw;
        }
    }

    template<typename Val, typename BucketIterator, typename Container>
    class base_iterator : public boost::iterator_facade<
            base_iterator<Val, BucketIterator, Container>,
            Val, boost::forward_traversal_tag>
    {
    public:
        base_iterator() : bucket_idx(-1), Container(nullptr) {
        }

        explicit base_iterator(Container* base_ptr, BucketIterator local_iterator, int bucket_idx) :
                base_ptr(base_ptr), local_iterator(local_iterator), bucket_idx(bucket_idx)
        {
        }

    private:
        friend class boost::iterator_core_access;
        void increment() {
            bool local_finished = false;
            {
                std::lock_guard<Locable> guard(bucket_mutexes[bucket_idx]);
                ++local_iterator;
                local_finished = local_iterator == base_ptr->buckets[bucket_idx].end();
            }

            if (local_finished) {
                ++bucket_idx;
                if (bucket_idx < base_ptr->buckets.size()) {
                    std::lock_guard<Locable> guard(bucket_mutexes[bucket_idx]);
                    local_iterator = base_ptr->buckets[bucket_idx].begin();
                }
            }
        }

        bool equal(base_iterator<Val, BucketIterator, Container> const& other) const {
            return this->base_ptr == other.base_ptr
                   && this->bucket_idx == other.bucket_idx
                   && this->local_iterator == other.local_iterator;
        }

        Val& dereference() const {
            return *local_iterator;
        }

        BucketIterator local_iterator;
        int bucket_idx;
        Container* base_ptr;
    };

    typedef base_iterator<value_type, local_iterator,
            concurrent_unordered_map<Key,
                    Value,
                    Hash,
                    KeyEqual,
                    Allocator,
                    Locable>> iterator;

    typedef base_iterator<const value_type, const_local_iterator,
            const concurrent_unordered_map<Key,
                    Value,
                    Hash,
                    KeyEqual,
                    Allocator,
                    Locable>> const_iterator;

    iterator find(const Key& key) {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        auto rez = buckets[bucket_id].find(key);
        if (rez != buckets[bucket_id].end()) {
            return iterator(this, rez, bucket_id);
        } else {
            return iterator(this, buckets[buckets.size() - 1].end(), buckets.size());
        }
    }

    const_iterator find(const Key& key) const {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        auto rez = buckets[bucket_id].find(key);
        return const_iterator(this, rez, bucket_id);
    }

    std::pair<iterator,iterator> equal_range(const Key& key) {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        auto rez = buckets[bucket_id].equal_range(key);
        return make_pair(iterator(this, rez.first, bucket_id),
                         iterator(this, rez.second, bucket_id));
    }

    std::pair<const_iterator,const_iterator> equal_range(const Key& key) const {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        auto rez = buckets[bucket_id].equal_range(key);
        return make_pair(const_iterator(this, rez.first, bucket_id),
                         const_iterator(this, rez.second, bucket_id));
    }


    std::pair<iterator, bool> insert(const value_type& val) {
        size_type bucket_id = bucket(val.first);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        auto result = buckets[bucket_id].insert(val);
        return make_pair(iterator(this, result.first, bucket_id), result.second);
    }

    template<class P>
    std::pair<iterator, bool> insert(P&& val) {
        size_type bucket_id = bucket(val.first);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        auto result = buckets[bucket_id].insert(val);
        return make_pair(iterator(this, result.first, bucket_id), result.second);
    }

    template<class InputIterator>
    void insert(InputIterator first, InputIterator last) {
        for (auto i = first; i != last; ++i) {
            insert(*i);
        }
    }

    void insert(std::initializer_list<value_type> items) {
        insert(items.begin(), items.end());
    }
    template <class M>
    std::pair<iterator, bool> insert_or_assign(const key_type& key, M&& obj) {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        auto iter = buckets[bucket_id].find(key);
        if (iter != buckets[bucket_id].end()) {
            iter->second = obj;
            return make_pair(iterator(this, iter, bucket_id), false);
        }

        return insert(std::make_pair(key, std::forward<M>(obj)));
    }

    template<class K, class... Args>
    bool emplace(K key, Args&&... args) {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        return buckets[bucket_id].emplace(key, args...).second;
    }

    iterator erase(const_iterator pos) {
        size_type bucket_id = bucket(pos->first);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        auto next = buckets[bucket_id].erase(pos.local_iterator);
        return iterator(this, next, bucket_id);
    }

    iterator erase(const_iterator first, const_iterator last) {
        iterator result;
        for (auto i = first; i != last; ++i) {
            result = erase(i);
        }
        return result;
    }

    size_type erase(const key_type& key) {
        size_type bucket_id = bucket(key);
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        return buckets[bucket_id].erase(key);
    }

    iterator begin() {
        std::lock_guard<Locable> guard(bucket_mutexes[0]);
        return iterator(this, buckets[0].begin(), 0);
    }

    iterator end() {
        std::lock_guard<Locable> guard(bucket_mutexes[buckets.size() - 1]);
        return iterator(this, buckets[buckets.size() - 1].end(), buckets.size());
    }

    const_iterator cbegin() const {
        std::lock_guard<Locable> guard(bucket_mutexes[0]);
        return iterator(this, buckets[0].cbegin(), 0);
    }

    const_iterator cend() const {
        std::lock_guard<Locable> guard(bucket_mutexes[buckets.size() - 1]);
        return iterator(this, buckets[buckets.size() - 1].cend(), buckets.size());
    }

    const_iterator begin() const {
        return cbegin();
    }

    const_iterator end() const {
        return cend();
    }

    size_type bucket_count() const {
        return buckets.size();
    }

    size_type max_bucket_count() const {
        return bucket_count();
    }
    size_type bucket_size(const size_type bucket_id) const {
        std::lock_guard<Locable> guard(bucket_mutexes[bucket_id]);
        return buckets[bucket_id].size();
    }

    size_type bucket(const Key& key) const {
        return hash(key) % buckets.size();
    }

    float load_factor() const {
        float result = 0;
        for (size_type i = 0; i < buckets.size(); ++i) {
            std::lock_guard<Locable> guard(bucket_mutexes[i]);
            result += buckets[i].load_factor();
        }
        return result / buckets.size();
    }

    float max_load_factor() const {
        float result = std::numeric_limits<float>::min();
        for (size_type i = 0; i < buckets.size(); ++i) {
            std::lock_guard<Locable> guard(bucket_mutexes[i]);
            result = std::max(result, buckets[i].max_load_factor());
        }
        return result;
    }

    void max_load_factor(float ml) {
        for (size_type i = 0; i < buckets.size(); ++i) {
            std::lock_guard<Locable> guard(bucket_mutexes[i]);
            buckets[i].max_load_factor(ml);
        }
    }

    void rehash(size_type count) {
        size_type per_bucket_count = (count + buckets.size() - 1) / buckets.size();
        for (size_type i = 0; i < buckets.size(); ++i) {
            std::lock_guard<Locable> guard(bucket_mutexes[i]);
            buckets[i].rehash(per_bucket_count);
        }
    }

    void reserve(size_type count) {
        rehash(std::ceil(count / max_load_factor()));
    }

    hasher hash_function() const {
        return hash;
    }
    key_equal key_eq() const {
        return equal;
    }

private:
    friend class base_iterator<value_type, local_iterator,
            concurrent_unordered_map<Key,
                    Value,
                    Hash,
                    KeyEqual,
                    Allocator,
                    Locable>>;

    friend class base_iterator<const value_type, const_local_iterator,
            const concurrent_unordered_map<Key,
                    Value,
                    Hash,
                    KeyEqual,
                    Allocator,
                    Locable>>;

    void lock_all() {
        boost::lock(bucket_mutexes.begin(), bucket_mutexes.end());
    }

    void unlock_all() {
        unlock_all(bucket_mutexes.begin(), bucket_mutexes.end());
    }

    template<class InputIterator>
    void unlock_all(InputIterator first, InputIterator last) {
        for (auto i = first; i != last; ++i) {
            i->unlock();
        }
    }
};

constexpr int TEST_ITERATIONS = 1000 * 1000;

template<typename M, typename Operation>
void test_thread(M& m, Operation op, float write_ratio, unsigned seed) {
    std::minstd_rand e(seed);
    std::uniform_int_distribution<unsigned> key_dist(1, std::numeric_limits<unsigned>::max());
    std::uniform_int_distribution<unsigned> value_dist(1, std::numeric_limits<unsigned>::max());
    std::uniform_int_distribution<unsigned> write_ratio_dist(1, 100);
    for (int i = 0; i < TEST_ITERATIONS; ++i) {
        unsigned key = key_dist(e);
        unsigned val = value_dist(e);
        bool need_write = write_ratio_dist(e) <= static_cast<unsigned>(write_ratio * 100);
        op(m, key, val, need_write);
    }
}

long long get_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

template<typename M, typename Operation>
void run_test(M& m, Operation op, const std::string& description,
              unsigned thread_count, float write_ratio) {
    auto start_time = get_time_ms();

    std::vector<std::thread> threads;
    static const unsigned magic_prime = 1000 * 1000 * 1000 + 7;
    threads.reserve(thread_count);
    for (unsigned i = 0; i < thread_count; ++i) {
        std::thread t([&m, op, write_ratio, i] {
                test_thread(m, op, write_ratio, i * magic_prime);
            });
        threads.push_back(std::move(t));
    }

    for (int i = 0; i < thread_count; ++i) {
        threads[i].join();
    }

    std::cout << description << " test is done for "
              << get_time_ms() - start_time
              << "ms." << std::endl;
}

int main(int argc, char **argv) {
    srand(13213);

    for (unsigned write_faction_denominator : { 1, 2, 5, 10, 20 }) {
        std::cout << "Write fraction: 1/" << write_faction_denominator << std::endl;
        for (unsigned thread_count : { 1, 2, 4, 8, 16, 32, 64 }) {
            std::cout << "Thread count: " << thread_count << std::endl;

            boost::synchronized_value<std::unordered_map<unsigned, unsigned>> m0;
            m0->reserve(TEST_ITERATIONS);
            run_test(m0, [](boost::synchronized_value<std::unordered_map<unsigned, unsigned>>& m,
                            unsigned key, unsigned val, bool need_write) {
                         auto i = m->find(key);

                         if (need_write) {
                             m->emplace(key, val);
                         }
                     }, "Default syncronized map", thread_count, 1.0 / write_faction_denominator);

            std::concurrent_unordered_map<unsigned, unsigned> m1(TEST_ITERATIONS);
            run_test(m1, [](std::concurrent_unordered_map<unsigned, unsigned>& m,
                            unsigned key, unsigned val, bool need_write) {
                         unsigned old;
                         m.find(key, old);

                         if (need_write) {
                             m.emplace(key, val);
                         }
                     }, "std concurrent hash map", thread_count, 1.0 / write_faction_denominator);

            concurrent_unordered_map<unsigned, unsigned> m2(thread_count);
            m2.reserve(TEST_ITERATIONS);
            run_test(m2, [](concurrent_unordered_map<unsigned, unsigned>& m,
                            unsigned key, unsigned val, bool need_write) {
                         m.find(key);

                         if (need_write) {
                             m.emplace(key, val);
                         }
                     }, "Collision list based syncronized map", thread_count, 1.0 / write_faction_denominator);
            cuckoohash_map<unsigned, unsigned> m3(TEST_ITERATIONS);
            run_test(m3, [](cuckoohash_map<unsigned, unsigned>& m,
                            unsigned key, unsigned val, bool need_write) {
                         unsigned old;
                         m.find(key, old);

                         if (need_write) {
                             m.insert(key, val);
                         }
                     }, "libcuckoo hash map", thread_count, 1.0 / write_faction_denominator);
            folly::ConcurrentHashMap<unsigned, unsigned> m4(TEST_ITERATIONS);
            run_test(m4, [](folly::ConcurrentHashMap<unsigned, unsigned>& m,
                            unsigned key, unsigned val, bool need_write) {
                         m.find(key);

                         if (need_write) {
                             m.emplace(key, val);
                         }
                     }, "folly ConcurrentHashMap", thread_count, 1.0 / write_faction_denominator);

        }
        std::cout << std::endl;
    }

    return 0;
}
