#pragma once
// The OpenGL functions the raylib backend calls beside rlgl, for the platform it is built for, and the few places
// where desktop OpenGL 3.3 and OpenGL ES 3 differ for what the backend does.
//
// On the desktop the functions are raylib's own GL loader's, glad: raylib compiles its function pointers in, leaves
// them visible to the program it is linked into, and loads them once the window exists. On the web platform the GL is
// OpenGL ES 3 (WebGL 2), whose functions the platform provides by name, with no loader.
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include "external/glad.h"
#endif

namespace BRITE::Backends::Raylib::Gl {

// The framebuffer bound draws into no colour attachment: a depth-only target. ES 3 has no glDrawBuffer, only the
// plural, which says the same of a list of one.
inline void DrawNoColour() {
#if defined(__EMSCRIPTEN__)
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
#else
    glDrawBuffer(GL_NONE);
#endif
}

// The type a depth texture's upload is described with -- there is no data, but the call names a type. ES 3 takes a
// DEPTH_COMPONENT24 texture only with GL_UNSIGNED_INT; desktop GL takes any, and has always been given GL_FLOAT here.
#if defined(__EMSCRIPTEN__)
inline constexpr GLenum DepthUploadType = GL_UNSIGNED_INT;
#else
inline constexpr GLenum DepthUploadType = GL_FLOAT;
#endif

// Whether a texture can be read back whole, as floats (glGetTexImage): desktop GL only. Where it cannot, a readback
// of a target is refused, as one of a target that does not exist is.
#if defined(__EMSCRIPTEN__)
inline constexpr bool CanReadTexturesBack = false;
#else
inline constexpr bool CanReadTexturesBack = true;
#endif

} // namespace BRITE::Backends::Raylib::Gl
