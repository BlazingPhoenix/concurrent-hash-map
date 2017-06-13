#include <concurrent_hash_map/concurrent_hash_map.hpp>

#include <unordered_map>

using namespace std;

struct orders_book {
    using order_key = string;
    using order_amount = std::size_t;
    enum class order_type: std::size_t { BID, ASK };
    using order_t = std::pair<order_type, order_amount>;

private:
    concurrent_unordered_map<order_key, order_t> orders;

// For debug only: optimistic attempt to catch concurrent use of snapshot() function.
#ifndef NDEBUG
    atomic<size_t> uses_;
    struct in_use {
        atomic<size_t>& uses_;

        in_use(atomic<size_t>& uses)
                : uses_(uses)
        {
            ++ uses_;
        }

        ~in_use() {
            -- uses_;
        }
    };

    in_use set_in_use() noexcept {
        return {uses_};
    }
    void validate() const {
        const size_t v = uses_;
        assert(v == 0);
    }
#else
    struct in_use {};
    static in_use set_in_use() noexcept { return {}; }
    static void validate() noexcept {}
#endif

    orders_book(const orders_book&) = delete;
    orders_book& operator=(const orders_book&) = delete;

public: // Therad safe:
    orders_book() = default;

    void place(const order_key& o, order_t&& v) {
        const auto guard = set_in_use();
        orders.insert_or_assign(o, std::move(v));
    }

    auto try_get(const order_key& o) const {
        return orders.find(o);
    }

    auto try_bet(const order_key& o) {
        const auto guard = set_in_use();
        return orders.erase(o);
    }

public: // Not thread safe!
    unordered_map<order_key, order_t> snapshot() {
        validate();
        auto v = std::move(orders.lock_table());
        validate();
        unordered_map<order_key, order_t > ret{v.begin(), v.end()};
        validate();
        return ret;
    }
};

int main() {
    orders_book orders;
    orders.place("123", make_pair(orders_book::order_type::ASK, 100));
    orders.place("1234", make_pair(orders_book::order_type::ASK, 13));
    auto order = orders.try_get("123");
    assert(order && order->second == 100);
    assert(1 == orders.try_bet("1234"));
    {
        auto state = orders.snapshot();
        assert(state["123"].second == 100 && state.count("1234") == 0);
        assert(state.size() == 1);
    }
    return 0;
}
