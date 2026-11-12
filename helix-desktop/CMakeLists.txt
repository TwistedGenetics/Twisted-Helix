# HELIX desktop: JUCE VST3 / AU / Standalone.
#
# JUCE is taken from HELIX_JUCE_DIR if set (a local checkout), otherwise fetched (tag 8.0.4).
# JUCE is dual licensed (AGPLv3 / commercial): check its licence terms before distributing a binary.
cmake_minimum_required(VERSION 3.22)

set(HELIX_JUCE_DIR "" CACHE PATH "Path to a JUCE checkout (optional)")
if(HELIX_JUCE_DIR)
  add_subdirectory(${HELIX_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

set(HELIX_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND HELIX_FORMATS AU)
endif()

juce_add_plugin(HelixDesktop
  PRODUCT_NAME "HELIX"
  COMPANY_NAME "Twisted Genetics"
  BUNDLE_ID "com.twistedgenetics.helix"
  PLUGIN_MANUFACTURER_CODE TwGn
  PLUGIN_CODE Hlx1
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  FORMATS ${HELIX_FORMATS})

target_sources(HelixDesktop PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp)

target_compile_definitions(HelixDesktop PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(HelixDesktop
  PRIVATE helix-core juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
