#pragma once
// Loads .nam files: metadata (cheap) and the inference network (expensive).

#include <memory>
#include <vector>
#include "NAMEngine/NamModel.h"
#include "NAMMetadata/CaptureMetadata.h"

namespace namknobs
{
struct LoadedCapture
{
    std::filesystem::path file;
    CaptureMetadata meta;
    std::unique_ptr<NamModel> model;
};

class NamLoader
{
public:
    static bool isNamFile (const std::filesystem::path& p);

    /** Recursively collects .nam files from a mix of files and folders (drag & drop). */
    static std::vector<std::filesystem::path> collectNamFiles (const std::vector<std::filesystem::path>& dropped);

    /** Reads metadata + builds the network. Throws std::runtime_error on failure. */
    static LoadedCapture load (const std::filesystem::path& file);
};
} // namespace namknobs
