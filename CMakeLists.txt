cmake_minimum_required(VERSION 3.11)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)
conan_basic_setup(TARGETS)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(game_server
    	src/main.cpp
    	src/http_server.cpp
    	src/http_server.h
    	src/sdk.h
    	src/model.h
    	src/model.cpp
    	src/tagged.h
    	src/boost_json.cpp
    	src/json_loader.h
    	src/json_loader.cpp
    	src/request_handler.cpp
    	src/request_handler.h
    	src/logger.h
    	src/logger.cpp
    	src/ticker.h
    	src/loot_generator.h
    	src/loot_generator.cpp
    	src/frontend_info.h
	src/collision_detector.h
	src/collision_detector.cpp
	src/geom.h
	src/model_serialization.h
	src/postgres.h
	src/postgres.cpp
)

target_link_libraries(game_server PUBLIC CONAN_PKG::boost Threads::Threads CONAN_PKG::libpq CONAN_PKG::libpqxx)
