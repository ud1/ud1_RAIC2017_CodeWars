cmake_minimum_required(VERSION 2.6)
project(test)

SET(CMAKE_CXX_FLAGS "-D_LINUX -std=c++14 -O3 -g -Wall -Wno-unknown-pragmas -DENABLE_LOGGING")

include_directories(/media/denis/tmp/bb_ai/nanovg/nanovg/src/)

file(GLOB_RECURSE csimplesocket_Sources
    csimplesocket/*.cpp csimplesocket/*.h)
    
file(GLOB_RECURSE model_Sources
    model/*.cpp model/*.h)
    
add_executable(test main.cpp /media/denis/tmp/bb_ai/nanovg/nanovg/src/nanovg.c ${csimplesocket_Sources} ${model_Sources} 
Simulator.cpp Simulator.hpp

MyStrategy.cpp MyStrategy.h
RemoteProcessClient.cpp RemoteProcessClient.h
Runner.h
Strategy.cpp Strategy.h
Strat.cpp Strat.hpp
OldStrat.cpp OldStrat.hpp
)

install(TARGETS test RUNTIME DESTINATION bin)

target_link_libraries(test
    GL GLEW SDL2 SDL2_image boost_system boost_timer
)

