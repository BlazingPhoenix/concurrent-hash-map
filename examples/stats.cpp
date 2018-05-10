#include <concurrent_hash_map/concurrent_hash_map.hpp>

#include <random>
#include <iostream>
#include <utility>

void process_stuff(size_t id) {
    std::cout << "Updated counter : " << id << std::endl;
}

void process_stats(std::concurrent_unordered_map<unsigned long long, size_t>::unordered_map_view&& view) {
    std::cout << "Statistic counters final state" << std::endl;
    for (auto i = view.begin(); i != view.end(); ++i) {
        std::cout << i->first << " " << i->second << std::endl;
    }
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
            std::default_random_engine e1;
            std::uniform_int_distribution<int> uniform_dist(1, 5);
            for (auto i = 0; i < 10; ++i) {
                auto id = uniform_dist(e1);
                stats.emplace_or_visit(
                        id,
                        [](auto& v){ ++v; },
                        0
                );

                process_stuff(id);
            }
        });
    }

    for (auto& t: threads) {
        t.join();
    }

    process_stats(std::move(stats.make_unordered_map_view()));
}
