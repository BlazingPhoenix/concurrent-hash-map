find_package(Boost COMPONENTS system thread REQUIRED)

include_directories(..)
include_directories(${Boost_SYNC_INCLUDE_DIRS})

add_executable(user_session_cache user_session_cache.cpp)
target_link_libraries(user_session_cache
  PRIVATE ${Boost_SYSTEM_LIBRARY}
  PRIVATE ${Boost_THREAD_LIBRARY}
  PRIVATE pthread
  )

add_executable(event_processor event_processor.cpp)
target_link_libraries(event_processor
  PRIVATE ${Boost_SYSTEM_LIBRARY}
  PRIVATE ${Boost_THREAD_LIBRARY}
  PRIVATE pthread)

add_executable(stats stats.cpp)
target_link_libraries(stats
  PRIVATE ${Boost_SYSTEM_LIBRARY}
  PRIVATE ${Boost_THREAD_LIBRARY}
  PRIVATE pthread)
