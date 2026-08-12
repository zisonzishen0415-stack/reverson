if(DEFINED JUCE_ROOT AND NOT JUCE_ROOT STREQUAL "")
    add_subdirectory(${JUCE_ROOT} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ReversonVST
    VENDOR_NAME "Reverson"
    VERSION 1.0.0
    PLUGIN_MANUFACTURER_CODE Revs
    PLUGIN_CODE Revn
    PRODUCT_NAME "Reverson"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)
juce_generate_juce_header(ReversonVST)
set_source_files_properties(PluginEditor.cpp PROPERTIES GENERATED TRUE)
target_sources(ReversonVST PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_include_directories(ReversonVST PRIVATE ${CMAKE_SOURCE_DIR}/core)
target_link_libraries(ReversonVST PRIVATE reverson_core)
