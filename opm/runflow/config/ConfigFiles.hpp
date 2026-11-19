/*
  Copyright 2026 OPM-OP AS

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPM_RUNFLOW_CONFIG_FILES_HPP
#define OPM_RUNFLOW_CONFIG_FILES_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Json { class JsonObject; }

namespace Opm::RunFlow {

/// One configuration file to read.
struct ConfigFile
{
    std::filesystem::path path;
    bool required = false; ///< a missing file is an error (one the user named), not skipped
};

/// runflow.json, in the order read, each overriding the one before:
///   1. the installation's: <sysconfdir>/opm/runflow.json, found from where
///      the running program is (installationConfigFile());
///   2. the user's: ~/.runflow.json;
///   3. the current directory's: $PWD/runflow.json, e.g. a project's settings
///      next to its decks;
///   4. $RUNFLOW_CONFIG, if set (required: the user named it); a relative
///      path is taken from the current directory.
/// Missing files are skipped, except the one $RUNFLOW_CONFIG names.
std::vector<ConfigFile> configFiles();

/// The installation's runflow.json: the program's directory, then the path
/// from the installation's bin directory to its sysconfdir (fixed when
/// opm-common is built: "../etc" for /opt/opm/2026.04, "../../etc" for /usr),
/// then opm/runflow.json. A moved installation keeps its file.
std::filesystem::path installationConfigFile();

/// The running program's own path; empty if the system does not say.
std::filesystem::path executablePath();

/// A path from a configuration file: a leading "~" is the home directory, and
/// a relative path is relative to `base` (the directory of the file it is from).
std::filesystem::path resolveConfigPath(const std::string& value,
                                        const std::filesystem::path& base);

/// A list of paths separated by ':' (as PYTHONPATH), each resolved as by
/// resolveConfigPath().
std::string resolveConfigPathList(const std::string& value,
                                  const std::filesystem::path& base);

/// A string setting. Throws std::runtime_error naming `setting` and `origin`
/// if `value` is not a string.
std::string readString(const Json::JsonObject& value, const std::filesystem::path& origin,
                       const std::string& setting);

/// A list-of-strings setting. Throws std::runtime_error naming `setting` and
/// `origin` if `value` is not a list of strings.
std::vector<std::string> readStringList(const Json::JsonObject& value,
                                        const std::filesystem::path& origin,
                                        const std::string& setting);

/// A program setting (flow, launchinfo, launcher, bsub): a value with a '/'
/// is a path, resolved as by resolveConfigPath(); a bare name (e.g. "mpirun")
/// stays a name, found when it is used (findProgram()).
std::string resolveProgram(const std::string& value, const std::filesystem::path& base);

/// A launcher's options from one file ("local.launcher_options",
/// "lsf.launcher_options"): a list of strings, where "@inherit" stands for
/// `earlier` (listWithInherited()), and which must contain {np}, the number of
/// processes. Throws std::runtime_error otherwise.
std::vector<std::string> readLauncherOptions(const Json::JsonObject& value,
                                             const std::vector<std::string>& earlier,
                                             const std::filesystem::path& origin,
                                             const std::string& setting,
                                             std::vector<std::string>& warnings);

/// In a list setting that a later file replaces whole (search_dirs,
/// lsf.options), the entry that stands for the list the earlier files gave,
/// as $PATH does in PATH="$HOME/bin:$PATH":
///
///   "search_dirs": ["~/opm/builds", "@inherit"]   mine, then the earlier ones
///   "search_dirs": ["@inherit", "~/opm/builds"]   the earlier ones, then mine
///   "search_dirs": ["~/opm/builds"]               only mine
inline constexpr std::string_view inheritMarker = "@inherit";

/// The new value of a list setting from one file: `entries`, each made an item
/// by `convert`, with the first inheritMarker replaced by `earlier` (empty for
/// the first file that gives the setting). A further marker is dropped, with
/// a warning naming `setting` and `origin`.
template <class T, class Convert>
std::vector<T> listWithInherited(const std::vector<std::string>& entries,
                                 const std::vector<T>& earlier, Convert convert,
                                 const std::string& setting,
                                 const std::filesystem::path& origin,
                                 std::vector<std::string>& warnings)
{
    std::vector<T> result;
    bool inherited = false;
    for (const auto& entry : entries) {
        if (entry != inheritMarker) {
            result.push_back(convert(entry));
        } else if (!inherited) {
            result.insert(result.end(), earlier.begin(), earlier.end());
            inherited = true;
        } else {
            warnings.push_back(origin.string() + ": \"" + setting + "\": \""
                               + std::string(inheritMarker) + "\" more than once; only the first counts");
        }
    }
    return result;
}

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_CONFIG_FILES_HPP
