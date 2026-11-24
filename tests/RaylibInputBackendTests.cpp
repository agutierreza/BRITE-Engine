// RaylibInputBackend's mouse wheel: what raylib's platform layer hands raylib
// arrives through the backend on the same axis, with the same sign.
//
// raylib learns of a wheel from GLFW's scroll callback, which it installs on its
// window in InitWindow. The case calls that very callback, as GLFW does when the
// wheel moves, under a hidden window (never shown, never focused), so the whole
// path from the platform layer to the backend's reads is raylib's own.
// glfwSetScrollCallback returns the callback it replaces, which is how the case
// finds raylib's -- and it puts it straight back. Beside the case is the
// mutation that turns it red; it was run.

#include <Backends/Raylib/RaylibInputBackend.hpp>
#include <gtest/gtest.h>

#include <raylib.h>

// The two GLFW functions the case needs, declared as GLFW's own header does.
// raylib builds GLFW into its library, but does not put GLFW's header on its
// users' include path.
extern "C" {
struct GLFWwindow;
typedef void (*GLFWscrollfun)(GLFWwindow* window, double xoffset, double yoffset);
GLFWwindow* glfwGetCurrentContext(void);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow* window, GLFWscrollfun callback);
}

namespace {

class RaylibInputBackendWheel : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(64, 64, "BRITE hidden input tests");
    }
    static void TearDownTestSuite() {
        ::CloseWindow();
    }

    // raylib's scroll callback, found by swapping it out and straight back in.
    static GLFWscrollfun RaylibsScrollCallback() {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window)
            return nullptr;
        const GLFWscrollfun raylibs = glfwSetScrollCallback(window, nullptr);
        glfwSetScrollCallback(window, raylibs);
        return raylibs;
    }

    BRITE::Backends::Raylib::RaylibInputBackend m_backend;
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
