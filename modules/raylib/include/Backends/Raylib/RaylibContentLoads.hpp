#pragma once

// raylib's file loads, routed through a ContentRoot.
//
// raylib reads every file it loads -- models, a glTF's buffers, images and textures, shader and text files -- through
// LoadFileData or LoadFileText (but an OBJ's material library: NOT ROUTED, below), and both hand the read to a
// callback when one is set. Routing installs those
// callbacks: a name raylib is given that lies inside the root (a content name, a relative name that normalises to one
// -- raylib looks a model's external texture up as "./<file>" -- or a path into the root's folder) is read by the
// root, so a strict root refuses a file its manifest lacks, or one whose size or SHA-256 differs. A path OUTSIDE the
// root is a read around the layer: it is reported to `outside`, and refused unless the routing says to read it plain.
//
// NOT ROUTED: an OBJ model's material library, and so its textures. raylib reads the .obj itself through
// LoadFileText (routed), then changes the working directory into the model's folder, where tinyobj opens the .mtl
// with fopen -- never through a callback, so neither the manifest nor `outside` sees it -- and raylib asks for each
// texture the library names by its bare relative name. A relative name is a content name here, never relative to the
// working directory, so a texture beside an OBJ in a subfolder is looked up at the root's top: a strict root refuses
// it, or reads the root's own file of that name. Ship a model that must be read through the root as glTF, whose
// buffers and images raylib reads through LoadFileData. Relative names are not resolved against the working directory
// to mend this: the .mtl would still be read around the root, and a content name would change meaning whenever a
// program's working directory lay inside its root.
//
// raylib's callbacks are process-wide, so one routing is in force at a time and its root must outlive it. Routing
// replaces whatever callbacks were set before it, and stopping it leaves raylib reading files itself. Built only when
// both the raylib and the content modules are.

#include <Content/ContentRoot.hpp>

#include <functional>
#include <string_view>

namespace BRITE::Raylib {

struct FileLoadRouting {
    /// Null stops routing: raylib reads files itself again.
    const Content::ContentRoot* root = nullptr;
    /// A path outside the root: refused (raylib then reports a failed load), or read as a plain file.
    bool refuseOutsideRoot = true;
    /// Told of every path raylib asked for outside the root, refused or not.
    std::function<void(std::string_view path)> outside;
};

/// Replaces the routing in force.
void RouteFileLoads(FileLoadRouting routing);

/// The routing in force (an empty one when there is none).
const FileLoadRouting& CurrentFileLoadRouting();

/// Routes for its lifetime, and puts back the routing that was in force when it began -- so a scope inside another
/// returns to the outer one's, and the outermost to none.
class ScopedFileLoadRouting {
  public:
    explicit ScopedFileLoadRouting(FileLoadRouting routing);
    ~ScopedFileLoadRouting();
    ScopedFileLoadRouting(const ScopedFileLoadRouting&) = delete;
    ScopedFileLoadRouting& operator=(const ScopedFileLoadRouting&) = delete;

  private:
    FileLoadRouting m_previous;
};

} // namespace BRITE::Raylib
