#pragma once

#include <cstddef>
#include <vector>

namespace migi
{

// Reads a read-only file that was bundled with the application. `path` is the
// logical path relative to the application's asset root, for example
// "shaders/triangle_vertex.shaderb".
//
// On desktop this reads "<cwd>/assets/<path>" (the layout produced by
// windows_app). On Android it reads the file out of the APK through the
// platform AAssetManager. The call aborts if the file cannot be read.
std::vector<std::byte> ReadFile(const char* path);

}
