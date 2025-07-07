cmake_minimum_required(VERSION 3.15)

# Add a build option for enabling examples (OFF by default for Magica minimal build)
option(TE_ADD_EXAMPLES "Add the example targets" OFF)

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

# Add JUCE modules
add_subdirectory(modules/juce)

# Add Tracktion Engine modules (core only)
add_subdirectory(modules)

if (TE_ADD_EXAMPLES)
    enable_testing()
    add_subdirectory(examples)
endif()
