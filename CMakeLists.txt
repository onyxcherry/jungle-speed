cmake_minimum_required(VERSION 3.28)
project(JungleSpeed)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Debug)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SFML COMPONENTS system window graphics CONFIG REQUIRED)

add_executable(JungleSpeedServer server.cpp)
add_executable(JungleSpeedClient test.cpp)
target_link_libraries(JungleSpeedServer PRIVATE Threads::Threads)
target_link_libraries(JungleSpeedServer PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(JungleSpeedClient PRIVATE Threads::Threads)
target_link_libraries(JungleSpeedClient PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(JungleSpeedClient PRIVATE sfml-system sfml-network sfml-graphics sfml-window)
