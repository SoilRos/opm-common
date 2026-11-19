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

#ifndef OPM_RUNFLOW_CANDIDATES_HPP
#define OPM_RUNFLOW_CANDIDATES_HPP

#include <opm/runflow/versions/Installation.hpp>
#include <opm/runflow/versions/VersionsConfig.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// The Flow executables that may be selected, in this order:
///   1. the configured versions (a "flow" that is a bare name is looked up in
///      `pathDirs`; one that is not found is a warning);
///   2. every executable the search.flow patterns match, pattern by pattern,
///      each pattern's matches in name order;
///   3. "flow" in each of `pathDirs` (the directories of PATH).
/// An executable found more than once is one installation, with every path,
/// name and way it was found. Nothing is run. Filesystem errors (an unreadable
/// directory, an executable that vanished after it was seen) go to `warnings`
/// and skip that entry; they never stop the search. Versions without their
/// own pythonpath get search.pythonpath's (applyDefaultPythonPath()).
std::vector<Installation> findCandidates(const VersionsConfig& config,
                                         const std::vector<std::filesystem::path>& pathDirs,
                                         std::vector<std::string>& warnings);

/// The directories of $PATH, in order; an empty entry is the current directory.
std::vector<std::filesystem::path> pathFromEnvironment();

/// Whether `path` is a regular file this user may execute.
bool isExecutableFile(const std::filesystem::path& path);

/// A program as configured (resolveProgram()): a path is used if it is an
/// executable; a bare name is looked up in `dirs`, in order. Nothing if not
/// found.
std::optional<std::filesystem::path> findProgram(const std::string& program,
                                                 const std::vector<std::filesystem::path>& dirs);

/// What `pattern` matches on the filesystem, as the shell matches *, ? and
/// [...], in name order; nothing if it matches nothing, also when a directory
/// on the way does not exist. If one exists but cannot be read, the matching
/// stops there and `unreadable` (if given) is set.
std::vector<std::filesystem::path> globMatches(const std::string& pattern,
                                               bool* unreadable = nullptr);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_CANDIDATES_HPP
