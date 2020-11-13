add_library(options STATIC
	include/options/options.h
	
	src/options.cpp)

target_include_directories(options PUBLIC include)
target_link_libraries(options PUBLIC)
target_link_libraries(options PRIVATE CLI11::CLI11)
