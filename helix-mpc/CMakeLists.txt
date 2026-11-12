# helix-mpc: the platform-independent MPC app logic and its desktop-side tools. The MPC plugin itself is built
# by the MPC Plugin SDK's tools/build.py (see build.sh), which compiles src/*.cpp.
add_library(helix-mpc-app STATIC app/HelixMpcApp.cpp)
target_include_directories(helix-mpc-app PUBLIC include)
target_link_libraries(helix-mpc-app PUBLIC helix-core)

add_executable(helix-mpc-ui tools/make_ui.cpp)
target_link_libraries(helix-mpc-ui PRIVATE helix-mpc-app)

# Native build of the real SDK plugin (.so) for the SDK's host simulator, when the SDK submodule is present.
set(HELIX_MPC_SDK ${PROJECT_SOURCE_DIR}/external/mpc-plugin-builder-sdk)
if(EXISTS ${HELIX_MPC_SDK}/framework/mpc_plugin.h AND NOT WIN32)
  add_library(helix-mpc-native SHARED src/HelixMpc.cpp)
  target_include_directories(helix-mpc-native PRIVATE ${HELIX_MPC_SDK}/framework)
  target_link_libraries(helix-mpc-native PRIVATE helix-mpc-app)
  set_target_properties(helix-mpc-native PROPERTIES CXX_VISIBILITY_PRESET hidden PREFIX "" OUTPUT_NAME helix-native)
endif()
