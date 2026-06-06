#include <fnd/FileSystem.h>

#include <fnd/Assert.h>

#include "AndroidApp.h"

#include <android/asset_manager.h>

#include <cstring>

namespace migi
{

std::vector<std::byte> ReadFile(const char* path)
{
    AAssetManager* manager = AndroidPlatformState().assetManager;
    MIGI_ASSERT(manager != nullptr, "Android asset manager is not initialized");

    // aapt2 packs every asset at the root of the APK assets directory keyed by
    // its file name, so the logical sub-directory in `path` (e.g. "shaders/")
    // is dropped and only the base name is used to open the asset.
    const char* name = path != nullptr ? path : "";
    const char* slash = std::strrchr(name, '/');
    if (slash != nullptr)
        name = slash + 1;

    AAsset* asset = AAssetManager_open(manager, name, AASSET_MODE_BUFFER);
    MIGI_ASSERT(asset != nullptr, "Cannot open APK asset");

    const off_t size = AAsset_getLength(asset);
    MIGI_ASSERT(size >= 0, "Cannot determine APK asset size");

    std::vector<std::byte> bytes(static_cast<size_t>(size));
    if (!bytes.empty())
    {
        const int read = AAsset_read(asset, bytes.data(), bytes.size());
        MIGI_ASSERT(read == static_cast<int>(bytes.size()), "Cannot read APK asset");
    }
    AAsset_close(asset);
    return bytes;
}

}
