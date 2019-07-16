add_library(concurrent_hash_map INTERFACE)

target_include_directories(concurrent_hash_map INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
        ${Boost_SYNC_INCLUDE_DIRS}
        )

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(concurrent_hash_map INTERFACE Threads::Threads)

install(
        FILES
        concurrent_hash_map.hpp
        DESTINATION
        ${CMAKE_INSTALL_PREFIX}/include
)
