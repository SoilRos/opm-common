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

#include <opm/runflow/versions/Candidates.hpp>

#include <opm/runflow/versions/PythonPath.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <system_error>

#include <glob.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

// An executable as found at `path`; nothing, with a warning, if its real
// path cannot be had (e.g. it was removed after it was seen, or a directory
// on the way became unreadable): one candidate less, not the end of runflow.
std::optional<Opm::RunFlow::Installation>
found(const fs::path& path, const std::string& how, std::vector<std::string>& warnings)
{
    std::error_code ec;
    auto canonical = fs::canonical(path, ec);
    if (ec) {
        warnings.push_back(how + ": skipped " + path.string() + ": " + ec.message());
        return std::nullopt;
    }
    Opm::RunFlow::Installation inst;
    inst.flow = std::move(canonical);
    inst.paths.push_back(path);
    inst.executableNames.push_back(path.filename().string());
    inst.foundBy.push_back(how);
    return inst;
}

// Adds `inst`, or merges it into the installation of the same executable.
void add(std::vector<Opm::RunFlow::Installation>& all, Opm::RunFlow::Installation inst)
{
    auto same = std::ranges::find(all, inst.flow, &Opm::RunFlow::Installation::flow);
    if (same != all.end())
        Opm::RunFlow::merge(*same, inst);
    else
        all.push_back(std::move(inst));
}

} // namespace

namespace Opm::RunFlow {

bool isExecutableFile(const fs::path& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec) && ::access(path.c_str(), X_OK) == 0;
}

std::vector<fs::path> pathFromEnvironment()
{
    std::vector<fs::path> dirs;
    const char* path = std::getenv("PATH");
    if (!path)
        return dirs;
    const std::string_view value = path;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = std::min(value.find(':', start), value.size());
        const auto dir = value.substr(start, end - start);
        dirs.emplace_back(dir.empty() ? std::string_view(".") : dir);
        start = end + 1;
    }
    return dirs;
}

std::optional<fs::path> findProgram(const std::string& program, const std::vector<fs::path>& dirs)
{
    if (program.find('/') != std::string::npos)
        return isExecutableFile(program) ? std::optional<fs::path>(program) : std::nullopt;
    for (const auto& dir : dirs) {
        if (isExecutableFile(dir / program))
            return dir / program;
    }
    return std::nullopt;
}

std::vector<fs::path> globMatches(const std::string& pattern, bool* unreadable)
{
    std::vector<fs::path> matches;
    // Stops on a directory that exists but cannot be read (e.g. EACCES); a
    // missing one (ENOENT, ENOTDIR) just matches nothing, as in the shell.
    // Not GLOB_ERR: it stops on every error, missing directories included.
    const auto stopOnUnreadable = [](const char*, int error) -> int {
        return error != ENOENT && error != ENOTDIR;
    };
    glob_t found{};
    const int result = ::glob(pattern.c_str(), 0, stopOnUnreadable, &found);
    if (unreadable)
        *unreadable = result == GLOB_ABORTED;
    if (result == 0) { // sorted by name
        for (std::size_t i = 0; i < found.gl_pathc; ++i)
            matches.emplace_back(found.gl_pathv[i]);
    }
    ::globfree(&found);
    return matches;
}

std::vector<Installation> findCandidates(const VersionsConfig& config,
                                         const std::vector<fs::path>& pathDirs,
                                         std::vector<std::string>& warnings)
{
    std::vector<Installation> all;

    for (const auto& [name, entry] : config.versions) {
        const auto flow = findProgram(entry.flow, pathDirs);
        if (!flow) {
            warnings.push_back(entry.origin.string() + ": version \"" + name
                               + "\": not an executable: " + entry.flow);
            continue;
        }
        auto inst = found(*flow, "config " + entry.origin.string(), warnings);
        if (!inst)
            continue;
        inst->names.push_back(name);
        inst->launchInfo = entry.launchInfo;
        inst->launcher = entry.launcher;
        if (!entry.pythonPath.empty()) {
            std::string joined;
            for (const auto& dir : entry.pythonPath)
                joined += (joined.empty() ? "" : ":") + dir.string();
            inst->pythonPath = joined;
        }
        add(all, std::move(*inst));
    }

    for (const auto& pattern : config.search.flow) {
        bool directories = false;
        bool unreadable = false;
        for (const auto& match : globMatches(pattern.string(), &unreadable)) {
            std::error_code ec;
            if (fs::is_directory(match, ec)) {
                directories = true;
                continue;
            }
            if (!isExecutableFile(match))
                continue;
            if (auto inst = found(match, "search " + pattern.string(), warnings))
                add(all, std::move(*inst));
        }
        if (unreadable)
            warnings.push_back("search.flow: cannot read a directory of " + pattern.string());
        if (directories) // patterns match executables, not where they are
            warnings.push_back("search.flow: " + pattern.string()
                               + " matches directories; for the executables in them, add \"/flow*\"");
    }

    for (const auto& dir : pathDirs) {
        if (!isExecutableFile(dir / "flow"))
            continue;
        if (auto inst = found(dir / "flow", "PATH", warnings))
            add(all, std::move(*inst));
    }
    for (auto& inst : all)
        applyDefaultPythonPath(inst, config.search.pythonPath, warnings);
    return all;
}

} // namespace Opm::RunFlow
