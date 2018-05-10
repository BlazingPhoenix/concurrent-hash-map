#include <concurrent_hash_map/concurrent_hash_map.hpp>

#include <map>
#include <experimental/string_view>
#include <iostream>

using namespace std;
using std::experimental::string_view;

struct user_t {
    string name;
    size_t age;
    size_t view_count;
};

void process_user(shared_ptr<user_t> user, size_t additional_views) {
    user->view_count += additional_views;
}

auto get_new_user() {
    user_t victor({"victor", 24});
    return make_pair("victor", make_shared<user_t>(victor));
}

auto get_request() {
    return make_pair("alex", 13);
}

void read_users_from_file(concurrent_unordered_map<string_view, shared_ptr<user_t>>::unordered_map_view& users) {
    user_t alex({"alex", 24});
    user_t alice({"alice", 21});
    users.insert(make_pair("alex", make_shared<user_t>(alex)));
    users.insert(make_pair("alice", make_shared<user_t>(alice)));
}

void cleanup(concurrent_unordered_map<string_view, shared_ptr<user_t>>::unordered_map_view& users) {
    users.clear();
}

void dump_to_file(concurrent_unordered_map<string_view, shared_ptr<user_t>>::unordered_map_view& users) {

}

void count_statistics(concurrent_unordered_map<string_view, shared_ptr<user_t>>::unordered_map_view& users) {
    map<size_t, size_t> stats;
    for (const auto& user : users) {
        stats[user.second->age]++;
    }

    cout << "User count by age stats" << endl;
    for (auto& stat : stats) {
        cout << stat.first << '=' << stat.second << endl;
    }
    cout << endl;
}

int main() {
    concurrent_unordered_map<string_view, shared_ptr<user_t> > users;
    // single threaded fill
    {
        auto unsafe_users = std::move(users.make_unordered_map_view());
        read_users_from_file(unsafe_users);
    }

    constexpr unsigned threads_count = 10;
    while(1) {
        // concurrent work:
        std::atomic<int> b{threads_count * 100500};
        thread threads[threads_count];

        for (auto& t: threads) {
            // processing users
            t = thread([&users, &b]() {
                while (--b > 0) {
                    auto [user_name, data] = get_request();
                    auto u = users.find(user_name);
                    if (!u) continue;

                    process_user(*u, data);
                }
            });
        }

        // accepting users
        while (--b > 0) {
            auto [new_user_name, user] = get_new_user();
            users.emplace(new_user_name, user);
        }

        for (auto& t: threads) {
            t.join();
        }

        // single threaded processing:
        auto unsafe_users = std::move(users.make_unordered_map_view());
        count_statistics(unsafe_users);
        dump_to_file(unsafe_users);
        cleanup(unsafe_users);
    }
}
