#include <concurrent_hash_map/concurrent_hash_map.hpp>

#include <utility>

std::pair<std::size_t, std::size_t> get_something() {
    return std::make_pair(1u, 2u);
}

void process_stuff(size_t id, size_t data) {

}

void process_stats(std::concurrent_unordered_map<unsigned long long, size_t>::unsynchronized_view&&) {

}

int main() {
    using namespace std;
    using id_t = unsigned long long;
    using use_count_t = size_t;

    concurrent_unordered_map<id_t, use_count_t> stats;

    constexpr unsigned threads_count = 10;
    thread threads[threads_count];
    for (auto& t: threads) {
        t = thread([&stats]() {
            while (1) {
                auto [id, data] = get_something();
                stats.emplace_or_visit(
                        id,
                        [](auto& v){ ++v; },
                        0
                );

                process_stuff(id, data);
            }
        });
    }

    for (auto& t: threads) {
        t.join();
    }

    process_stats(std::move(stats.get_unsynchronized_view()));
}
