project(JungleSpeed)
cmake_minimum_required(VERSION 3.1)

find_package(nlohmann_json CONFIG REQUIRED)

add_executable(JungleSpeedServer server.cpp)
add_executable(JungleSpeedClient client.cpp)
target_link_libraries(JungleSpeedServer PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(JungleSpeedClient PRIVATE nlohmann_json::nlohmann_json)
