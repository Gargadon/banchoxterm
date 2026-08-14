#pragma once
#include <QString>

// Portable-edition helpers.
//
// A portable install is one that keeps all of its data next to the
// executable instead of in the OS user-profile locations. It is activated
// by either launching with --portable or placing a portable.ini marker file
// next to banchoxterm.exe.

namespace AppPaths {

// True when running in portable mode (--portable arg or portable.ini marker).
bool isPortable();

// Directory that contains the running executable.
QString applicationDir();

// Base directory for config data (portable dir or OS config location).
QString configDir();

// Redirect QSettings to an INI file inside the portable dir. Call once from
// main() before any QSettings is constructed.
void applyPortableSettings();

} // namespace AppPaths
