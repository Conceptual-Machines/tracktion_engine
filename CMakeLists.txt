cmake_minimum_required(VERSION 3.15)

# Add a build option for enabling examples (OFF by default for Magica minimal build)
option(TE_ADD_EXAMPLES "Add the example targets" OFF)

# When ON, builds a tiny console app that links the TE modules so CI actually
# compiles the .cpp files (the modules themselves are header-only INTERFACE
# targets until a consumer pulls them in). See ci/compile_check.cpp and
# .github/workflows/build.yml.
option(TE_BUILD_COMPILE_CHECK "Build the CI compile-check target" OFF)

# Read version from VERSION.md
file(READ VERSION.md CURRENT_VERSION)
string(STRIP ${CURRENT_VERSION} CURRENT_VERSION)

project(tracktion_engine
    VERSION ${CURRENT_VERSION}
    DESCRIPTION "Tracktion Engine - Core modules only (minimal build for Magica DAW)"
    HOMEPAGE_URL "https://github.com/lucaromagnoli/tracktion_engine"
)

# JUCE options - disable examples and extras
option(JUCE_BUILD_EXTRAS "Build JUCE Examples" OFF)
option(JUCE_BUILD_EXAMPLES "Build JUCE Extras" OFF)

# Add JUCE modules.
# A host project (MAGDA) may already own JUCE and add it before this engine; in
# that case bind to the host's copy instead of adding a second one. Standalone
# builds of this repo still use the nested submodule.
if (NOT TARGET juce::juce_core)
    add_subdirectory(modules/juce)
endif()

# Add Tracktion Engine modules (core only)
add_subdirectory(modules)

if (TE_ADD_EXAMPLES)
    enable_testing()
    add_subdirectory(examples)
endif()

if (TE_BUILD_COMPILE_CHECK)
    juce_add_console_app(te_compile_check
        PRODUCT_NAME "te-compile-check")

    target_sources(te_compile_check PRIVATE ci/compile_check.cpp)

    target_compile_definitions(te_compile_check PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_PLUGINHOST_VST3=0
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0)

    target_link_libraries(te_compile_check PRIVATE
        tracktion::tracktion_engine
        tracktion::tracktion_core
        tracktion::tracktion_graph
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()
