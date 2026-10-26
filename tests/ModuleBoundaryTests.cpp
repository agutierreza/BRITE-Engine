// The framework's modules are optional in the build: a module that is switched off (BRITE_BUILD_<MODULE>=OFF) is not
// fetched, not compiled, and -- what this case can see from inside a test -- neither its own headers nor its
// third-party library's are on the include path of a consumer that links BRITEEngine. CMake hands this file which
// modules this configure builds (BRITE_TEST_MODULE_*), and each header's reachability must match.
//
// With every module on (the default) every header is reachable; a configure with modules off runs the same case and
// asks the opposite of those. So a module's include root leaking onto the core's -- or a third-party library fetched
// for a module that is off -- turns this red in that configure.

#include <gtest/gtest.h>

#if !defined(BRITE_TEST_MODULE_RAYLIB) || !defined(BRITE_TEST_MODULE_BOX2D) || !defined(BRITE_TEST_MODULE_BOX3D) ||    \
    !defined(BRITE_TEST_MODULE_AUDIO) || !defined(BRITE_TEST_MODULE_PERSISTENCE) ||                                    \
    !defined(BRITE_TEST_MODULE_DEBUG_UI) || !defined(BRITE_TEST_MODULE_CONTENT)
#error "CMakeLists.txt defines BRITE_TEST_MODULE_* for this file"
#endif

namespace {

// Whether each header is reachable from here: __has_include is a preprocessor test, so each is read into a constant.
#if __has_include(<Framework/Application.hpp>)
constexpr bool CORE_HEADER = true;
#else
constexpr bool CORE_HEADER = false;
#endif
#if __has_include(<entt/entt.hpp>)
constexpr bool ENTT_HEADER = true;
#else
constexpr bool ENTT_HEADER = false;
#endif
#if __has_include(<Backends/Raylib/RaylibRenderBackend.hpp>)
constexpr bool RAYLIB_MODULE_HEADER = true;
#else
constexpr bool RAYLIB_MODULE_HEADER = false;
#endif
#if __has_include(<raylib.h>)
constexpr bool RAYLIB_HEADER = true;
#else
constexpr bool RAYLIB_HEADER = false;
#endif
#if __has_include(<Systems/Box2DBackend.hpp>)
constexpr bool BOX2D_MODULE_HEADER = true;
#else
constexpr bool BOX2D_MODULE_HEADER = false;
#endif
#if __has_include(<box2d/box2d.h>)
constexpr bool BOX2D_HEADER = true;
#else
constexpr bool BOX2D_HEADER = false;
#endif
#if __has_include(<Systems/Box3DBackend.hpp>)
constexpr bool BOX3D_MODULE_HEADER = true;
#else
constexpr bool BOX3D_MODULE_HEADER = false;
#endif
#if __has_include(<box3d/box3d.h>)
constexpr bool BOX3D_HEADER = true;
#else
constexpr bool BOX3D_HEADER = false;
#endif
#if __has_include(<Audio/SceneSounds.hpp>)
constexpr bool AUDIO_MODULE_HEADER = true;
#else
constexpr bool AUDIO_MODULE_HEADER = false;
#endif
#if __has_include(<soloud.h>)
constexpr bool SOLOUD_HEADER = true;
#else
constexpr bool SOLOUD_HEADER = false;
#endif
#if __has_include(<Persistence/SaveStore.hpp>)
constexpr bool PERSISTENCE_MODULE_HEADER = true;
#else
constexpr bool PERSISTENCE_MODULE_HEADER = false;
#endif
#if __has_include(<physfs.h>)
constexpr bool PHYSFS_HEADER = true;
#else
constexpr bool PHYSFS_HEADER = false;
#endif
#if __has_include(<Content/ContentRoot.hpp>)
constexpr bool CONTENT_MODULE_HEADER = true;
#else
constexpr bool CONTENT_MODULE_HEADER = false;
#endif
#if __has_include(<Backends/Raylib/RaylibContentLoads.hpp>)
constexpr bool RAYLIB_CONTENT_HEADER = true;
#else
constexpr bool RAYLIB_CONTENT_HEADER = false;
#endif
#if __has_include(<rlImGui.h>)
constexpr bool RLIMGUI_HEADER = true;
#else
constexpr bool RLIMGUI_HEADER = false;
#endif

} // namespace

TEST(ModuleBoundary, EachModulesHeadersAreOnTheIncludePathExactlyWhenItIsBuilt) {
    // The core is always there.
    EXPECT_TRUE(CORE_HEADER);
    EXPECT_TRUE(ENTT_HEADER);

    // MUTATION: modules/raylib/include made a PUBLIC include root of the core -> reachable with raylib off, red there.
    EXPECT_EQ(RAYLIB_MODULE_HEADER, BRITE_TEST_MODULE_RAYLIB != 0);
    EXPECT_EQ(RAYLIB_HEADER, BRITE_TEST_MODULE_RAYLIB != 0);

    EXPECT_EQ(BOX2D_MODULE_HEADER, BRITE_TEST_MODULE_BOX2D != 0);
    EXPECT_EQ(BOX2D_HEADER, BRITE_TEST_MODULE_BOX2D != 0);

    EXPECT_EQ(BOX3D_MODULE_HEADER, BRITE_TEST_MODULE_BOX3D != 0);
    EXPECT_EQ(BOX3D_HEADER, BRITE_TEST_MODULE_BOX3D != 0);

    EXPECT_EQ(AUDIO_MODULE_HEADER, BRITE_TEST_MODULE_AUDIO != 0);
    EXPECT_EQ(SOLOUD_HEADER, BRITE_TEST_MODULE_AUDIO != 0);

    EXPECT_EQ(PERSISTENCE_MODULE_HEADER, BRITE_TEST_MODULE_PERSISTENCE != 0);
    EXPECT_EQ(PHYSFS_HEADER, BRITE_TEST_MODULE_PERSISTENCE != 0);

    EXPECT_EQ(RLIMGUI_HEADER, BRITE_TEST_MODULE_DEBUG_UI != 0);

    EXPECT_EQ(CONTENT_MODULE_HEADER, BRITE_TEST_MODULE_CONTENT != 0);
    // The raylib routing's header is in the raylib module's include root, so it is reachable with raylib on; it is
    // compiled -- and usable -- only with the content module too, which is what its own header includes.
    EXPECT_EQ(RAYLIB_CONTENT_HEADER, BRITE_TEST_MODULE_RAYLIB != 0);
}
