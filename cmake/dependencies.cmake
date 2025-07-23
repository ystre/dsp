set(DSP_DEPS_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../deps")

find_package(Boost REQUIRED)
find_package(fmt REQUIRED)

find_package(prometheus-cpp REQUIRED)
find_package(RdKafka REQUIRED)

# Nova
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)
