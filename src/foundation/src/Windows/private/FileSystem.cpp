#include <fnd/FileSystem.h>

#include <fnd/Assert.h>

#include <fstream>
#include <string>

namespace migi
{

std::vector<std::byte> ReadFile(const char* path)
{
    std::string fullPath = "assets/";
    fullPath += path != nullptr ? path : "";

    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    MIGI_ASSERT(file.is_open(), "Cannot open asset file");

    const std::ifstream::pos_type end = file.tellg();
    MIGI_ASSERT(end >= 0, "Cannot determine asset file size");

    std::vector<std::byte> bytes(static_cast<size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        MIGI_ASSERT(file.good(), "Cannot read asset file");
    }
    return bytes;
}

}
