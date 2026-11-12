#include "NamLoader.h"

#include <algorithm>
#include <cctype>

namespace namvx
{
bool NamLoader::isNamFile (const std::filesystem::path& p)
{
    auto ext = p.extension().string();
    std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return ext == ".nam";
}

std::vector<std::filesystem::path> NamLoader::collectNamFiles (const std::vector<std::filesystem::path>& dropped)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& p : dropped)
    {
        if (std::filesystem::is_directory (p, ec))
        {
            for (auto it = std::filesystem::recursive_directory_iterator (p, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment (ec))
                if (! ec && it->is_regular_file (ec) && isNamFile (it->path())) out.push_back (it->path());
        }
        else if (isNamFile (p))
            out.push_back (p);
    }
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

LoadedCapture NamLoader::load (const std::filesystem::path& file)
{
    LoadedCapture c;
    c.file = file;
    c.meta = readCaptureMetadata (file);
    c.model = std::make_unique<NamModel>();
    c.model->load (file);
    if (c.meta.expectedSampleRate <= 0) c.meta.expectedSampleRate = c.model->getModelSampleRate();
    return c;
}
} // namespace namvx
