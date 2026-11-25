// RaylibInputBackend's mouse wheel and key repeat: what raylib's platform layer
// hands raylib arrives through the backend as the interface says -- the wheel on
// the same axis with the same sign, a key's repeat as a repeat and its press as
// none.
//
// raylib learns of a wheel from GLFW's scroll callback, and of keys from its key
// callback, both installed on its window in InitWindow. Each case calls that
// very callback, as GLFW does when the wheel moves or a key is pressed or
// repeated, under a hidden window (never shown, never focused), so the whole
// path from the platform layer to the backend's reads is raylib's own.
// glfwSetScrollCallback and glfwSetKeyCallback return the callback they
// replace, which is how a case finds raylib's -- and puts it straight back.
// Beside each case is the mutation that turns it red; it was run.

#include <Backends/Raylib/RaylibInputBackend.hpp>
#include <gtest/gtest.h>

#include <raylib.h>

// The two GLFW functions the case needs, declared as GLFW's own header does.
// raylib builds GLFW into its library, but does not put GLFW's header on its
// users' include path.
extern "C" {
struct GLFWwindow;
typedef void (*GLFWscrollfun)(GLFWwindow* window, double xoffset, double yoffset);
typedef void (*GLFWkeyfun)(GLFWwindow* window, int key, int scancode, int action, int mods);
GLFWwindow* glfwGetCurrentContext(void);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow* window, GLFWscrollfun callback);
GLFWkeyfun glfwSetKeyCallback(GLFWwindow* window, GLFWkeyfun callback);
}

// GLFW's values for a key's actions and for the A key, from GLFW's header.
constexpr int GLFW_RELEASE_ACTION = 0;
constexpr int GLFW_PRESS_ACTION = 1;
constexpr int GLFW_REPEAT_ACTION = 2;
constexpr int GLFW_KEY_A_CODE = 65;

namespace {

// One hidden raylib window for each suite, opened before its first case and
// closed after its last.
class HiddenRaylibWindow : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(64, 64, "BRITE hidden input tests");
    }
    static void TearDownTestSuite() {
        ::CloseWindow();
    }

    BRITE::Backends::Raylib::RaylibInputBackend m_backend;
};

class RaylibInputBackendWheel : public HiddenRaylibWindow {
  protected:
    // raylib's scroll callback, found by swapping it out and straight back in.
    static GLFWscrollfun RaylibsScrollCallback() {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window)
            return nullptr;
        const GLFWscrollfun raylibs = glfwSetScrollCallback(window, nullptr);
        glfwSetScrollCallback(window, raylibs);
        return raylibs;
    }
};

class RaylibInputBackendKeyRepeat : public HiddenRaylibWindow {
  protected:
    // raylib's key callback, found by swapping it out and straight back in.
    static GLFWkeyfun RaylibsKeyCallback() {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window)
            return nullptr;
        const GLFWkeyfun raylibs = glfwSetKeyCallback(window, nullptr);
        glfwSetKeyCallback(window, raylibs);
        return raylibs;
    }
};

} // namespace

TEST_F(RaylibInputBackendWheel, TheScrollOffsetsGlfwReportsArriveOnTheSameAxesWithTheSameSigns) {
    // GLFW reports a wheel as (xoffset, yoffset): here half a notch to the left
    // and three notches toward the user. The interface's convention is GLFW's --
    // positive x left, positive y away from the user -- so the backend reads x =
    // 0.5 and y = -3.0, unchanged. Two different magnitudes, so an axis swapped
    // or a sign flipped cannot read the same.
    // MUTATION: x and y swapped in the backend -- x reads -3.0, red; x negated --
    // -0.5, red.
    ASSERT_TRUE(::IsWindowReady());
    const GLFWscrollfun raylibs = RaylibsScrollCallback();
    ASSERT_NE(raylibs, nullptr) << "raylib installs a scroll callback on its window";
    raylibs(glfwGetCurrentContext(), 0.5, -3.0);
    EXPECT_FLOAT_EQ(m_backend.GetMouseWheelMoveX(), 0.5f);
    EXPECT_FLOAT_EQ(m_backend.GetMouseWheelMoveY(), -3.0f);

    // raylib holds one frame's movement and clears it at its next poll, so the
    // backend reports each frame's movement once.
    ::PollInputEvents();
    EXPECT_FLOAT_EQ(m_backend.GetMouseWheelMoveX(), 0.0f);
    EXPECT_FLOAT_EQ(m_backend.GetMouseWheelMoveY(), 0.0f);
}

TEST_F(RaylibInputBackendKeyRepeat, ThePlatformsRepeatReadsAsARepeatAndThePressDoesNot) {
    // GLFW sends a held key as one press, then repeats, then a release. A is
    // pressed: the backend reads it down and NOT repeated -- the press is not a
    // repeat. raylib polls (its next frame), and GLFW repeats A: now repeated.
    // raylib polls again with no repeat in between: not repeated -- raylib's
    // flag lasts the frame it was set in, so the backend reports each frame's
    // repeat once. Then A is let go, and the window is left with nothing held.
    // MUTATION: the backend answering from raylib's IsKeyPressed -- the first
    // read is true, red; from IsKeyDown -- the first read is true, red.
    ASSERT_TRUE(::IsWindowReady());
    const GLFWkeyfun raylibs = RaylibsKeyCallback();
    ASSERT_NE(raylibs, nullptr) << "raylib installs a key callback on its window";
    using BRITE::KeyCode;

    raylibs(glfwGetCurrentContext(), GLFW_KEY_A_CODE, 0, GLFW_PRESS_ACTION, 0);
    EXPECT_TRUE(m_backend.IsKeyDown(KeyCode::A)) << "the press reached raylib";
    EXPECT_FALSE(m_backend.IsKeyRepeated(KeyCode::A)) << "the press is not a repeat";

    ::PollInputEvents();
    raylibs(glfwGetCurrentContext(), GLFW_KEY_A_CODE, 0, GLFW_REPEAT_ACTION, 0);
    EXPECT_TRUE(m_backend.IsKeyRepeated(KeyCode::A));
    EXPECT_FALSE(m_backend.IsKeyRepeated(KeyCode::B)) << "only the key that repeated";

    ::PollInputEvents();
    EXPECT_FALSE(m_backend.IsKeyRepeated(KeyCode::A)) << "a frame with no repeat reads none";

    raylibs(glfwGetCurrentContext(), GLFW_KEY_A_CODE, 0, GLFW_RELEASE_ACTION, 0);
    ::PollInputEvents();
    EXPECT_FALSE(m_backend.IsKeyDown(KeyCode::A));
}
