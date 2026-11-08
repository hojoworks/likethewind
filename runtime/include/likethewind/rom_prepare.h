#pragma once

#include <string>

namespace likethewind {

struct RomPrepareResult {
    bool ok = false;
    bool prepared = false;
    std::string rom_dir;
    std::string error;
};

// Release launch: look for <exe>/roms/pdrift.zip, verify it, and unpack the
// board's flat images into <exe>/roms/.prepared. Existing flat images in the
// roms folder are accepted too. No Python or command line is involved.
RomPrepareResult prepare_release_roms(const std::string &exe_dir);

// Absolute directory containing the running executable.
std::string executable_directory();

} // namespace likethewind
