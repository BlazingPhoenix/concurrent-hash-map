#include <concurrent_hash_map/concurrent_hash_map.hpp>

#include <algorithm>

int main() {
    using namespace std;
    using event_id = unsigned long long;
    struct event_data {
        event_data(const event_data&) = delete;
        event_data& operator=(const event_data&) = delete;
        time_t start;
        string description;
    };

    concurrent_unordered_map<event_id, unique_ptr<event_data> > events;

    // Getting unique events.
    auto event_generators = get_event_generators();
    for_each(execution::par_unseq, event_generators.begin(), event_generators.end(), [&events](auto& g) {
        while (1) {
            auto [event_name, data] = g.get_event();
            if (event_name.empty()) {
                break; // no more events
            }

            events.emplace_or_visit(event_name, [&data](unique_ptr<event_data>& v){
                if (v || v->priority() < data->priority()) {
                    std::swap(data, v);
                }
            }, unique_ptr<event_data>{});
        }
    });

    auto v = events.lock_table();
    for_each(execution::par_unseq, v.begin(), v.end(), [](auto& e) {
        process(e.first, std::move(e.second));
    });
}
