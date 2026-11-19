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

#ifndef OPM_RUNFLOW_INSTALLATION_HPP
#define OPM_RUNFLOW_INSTALLATION_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// What a Flow binary says about itself (describeFlow()). Each field is
/// empty if it did not say: older versions give the OPM version only.
struct BuildInfo
{
    std::optional<std::string> opmVersion; ///< e.g. "2026.04"
    std::optional<std::string> gitHash;
    std::optional<std::string> buildTime;
    std::optional<std::string> buildType;  ///< e.g. "Release"
    std::optional<std::string> mpi;        ///< e.g. "Open MPI v5.0.7", or "none"
};

/// One Flow version.
///
/// Invariant: `flow` is a canonical path (absolute, no symbolic links); it is
/// the identity of an installation, so an executable found twice (also
/// through a symbolic link) is one installation.
struct Installation
{
    std::filesystem::path flow;

    /// The paths it was found as, e.g. /usr/bin/flow for a symbolic link
    /// (opmlaunchinfo is looked for next to each, see deckResources()).
    std::vector<std::filesystem::path> paths;
    /// Names given in the configuration, e.g. "dev".
    std::vector<std::string> names;
    /// File names it was found as, e.g. "flowrc47" or "flow".
    std::vector<std::string> executableNames;

    /// Configured programs, as written (resolveProgram()): a path, or a name
    /// found when used.
    std::optional<std::string> launchInfo; ///< its opmlaunchinfo (findLaunchInfo())
    std::optional<std::string> launcher;   ///< its MPI launcher program (findLauncher())
    /// Directories separated by ':', put before PYTHONPATH when it runs: its
    /// own, or what search.pythonpath gives it.
    std::optional<std::string> pythonPath;

    /// How it was found, for users: "config FILE", "search_dirs DIR", "PATH".
    std::vector<std::string> foundBy;

    BuildInfo build; ///< filled by VersionRegistry when needed
};

/// Adds what `other` knows about the same installation to `target`: lists are
/// extended, and every configured field `other` sets replaces `target`'s.
/// Precondition: both describe the same executable (same `flow`).
void merge(Installation& target, const Installation& other);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_INSTALLATION_HPP
