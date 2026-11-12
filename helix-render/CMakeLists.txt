add_executable(helix-render src/main.cpp src/MidiFile.cpp)
target_link_libraries(helix-render PRIVATE helix-core)
target_include_directories(helix-render PRIVATE src)
