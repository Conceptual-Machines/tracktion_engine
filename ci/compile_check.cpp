// Minimal compile-check consumer for the Tracktion Engine modules.
// Linked into the te_compile_check console app target when TE_BUILD_COMPILE_CHECK
// is ON (used by .github/workflows/build.yml). The point is to force every
// .cpp in tracktion_core / tracktion_engine / tracktion_graph to actually
// compile under CI — JUCE module libraries are header-only INTERFACE targets
// until something downstream pulls them in.

#include <tracktion_engine/tracktion_engine.h>

int main()
{
    // Construct an Engine just to make sure the module's static init paths
    // run cleanly. Anything more would be a runtime test, not a build smoke.
    tracktion::engine::Engine engine { "te-compile-check" };
    return 0;
}
