#include <concurrent_hash_map/concurrent_hash_map.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <future>
#include <iostream>

using namespace std;
using event_id = unsigned long long;

struct event_data {
    event_data() = default;
    event_data(event_data&&) = default;
    event_data(const event_data&) = delete;
    event_data& operator=(const event_data&) = delete;
    event_data& operator=(event_data&&) = default;

    time_t start;
    std::string name;
    unsigned long long priority = 0;
};

struct event_generator {
    std::string name;
    std::default_random_engine e;
    std::uniform_int_distribution<unsigned long long> dist;

    event_generator(const std::string& name, unsigned seed)
    : name(name), e(seed), dist(1, 10)
    {
    }

    std::pair<unsigned long long, std::unique_ptr<event_data>> get_event() {
        event_data ev;
        ev.start = time(nullptr);
        ev.name = name;
        ev.priority = dist(e);
        return std::make_pair(dist(e), std::move(std::make_unique<event_data>(std::move(ev))));
    }
};

std::vector<event_generator> get_event_generators() {
    std::vector<event_generator> result;
    for (int i = 0; i < 10; ++i) {
        result.emplace_back("Gen " + std::to_string(i), i);
    }
    return result;
}

void process(event_id id, const event_data& data) {
    tm* start = localtime(&data.start);
    std::cout << "Id: " << id << " started at " << start->tm_hour << ":" << start->tm_min
              << " generator " << data.name << " priority " << data.priority << std::endl;
}

int main() {
    concurrent_unordered_map<event_id, unique_ptr<event_data> > events;

    // Getting unique events.
    auto event_generators = get_event_generators();
    std::vector<future<void>> handlers;
    for (auto& generator : event_generators) {
        auto res = std::async(std::launch::async, [&events, &generator]() {
            for (int i = 0; i < 10; ++i) {
                std::pair<unsigned long long, std::unique_ptr<event_data>> event(generator.get_event());

                events.emplace_or_visit(event.first, [&event](unique_ptr<event_data>& v) {
                    if (v || v->priority < event.second->priority) {
                        std::swap(event.second, v);
                    }
                }, unique_ptr<event_data>{});
            }
        });

        handlers.emplace_back(std::move(res));
    }

    for (auto& handler : handlers) {
        handler.wait();
    }

    auto v = events.make_unordered_map_view(true);
    for (auto& e : v) {
        process(e.first, *e.second);
    }

    return 0;
}
