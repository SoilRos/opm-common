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

#ifndef OPM_RUNFLOW_LAUNCHER_HPP
#define OPM_RUNFLOW_LAUNCHER_HPP

#include <opm/runflow/versions/Installation.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// The MPI launcher of one run: a program, found once and used by its
/// absolute path, so that the program that was checked is the one that runs,
/// also in a job whose PATH differs.
struct Launcher
{
    std::filesystem::path program;      ///< absolute
    std::vector<std::string> arguments; ///< its options, {np} and {slots} not yet replaced
    std::string how;                    ///< how the program was found, for users: "next to flow", ...

    /// The command: the program, then the arguments with {np} and {slots} replaced.
    std::vector<std::string> command(int np, int slots) const;
};

/// The launcher for runs of `flow`: `program` with `options` (the "launcher"
/// and "launcher_options" of "local" or of the scheduler's section). The
/// program is the version's own "launcher" if it has one, else `program`;
/// either is, as configured (resolveProgram()):
///   - a path (it has a '/'): used if it is an executable;
///   - a name: looked up as the shell would after `module load` of that Flow:
///     next to flow (in the directory of each path it was found as, then of
///     its real path), then in `pathDirs`.
/// Nothing if the program is not found.
std::optional<Launcher> findLauncher(const Installation& flow, const std::string& program,
                                     const std::vector<std::string>& options,
                                     const std::vector<std::filesystem::path>& pathDirs);

/// An MPI library, as far as it can be told from what programs print.
struct MpiIdentity
{
    std::string implementation; ///< "Open MPI", "MPICH" or "Intel MPI"
    std::string version;        ///< e.g. "5.0.7"
};

/// The MPI of Flow's `mpi=` line of --build-info, e.g. "Open MPI v5.0.7",
/// "MPICH Version: 4.1.2", "Intel(R) MPI Library 2021.10 for Linux* OS".
std::optional<MpiIdentity> identifyMpi(const std::string& text);

/// The result of checkLauncher(), for users.
struct LauncherCheck
{
    enum class Result {
        Match,           ///< the same MPI implementation and major.minor version
        FlowUnknown,     ///< Flow does not say its MPI (older Flow without --build-info)
        LauncherUnknown, ///< the launcher's --version is not understood
    };
    Result result;
    std::string text; ///< e.g. "Open MPI 5.0.7, as Flow's"
};

/// Checks that `launcher` belongs to the MPI `flow` was built with, by its
/// --version against Flow's mpi= (asked for at most 30 seconds). Throws
/// LaunchError when they differ in implementation or in major.minor version,
/// or when Flow was built without MPI: starting Flow with another MPI's
/// launcher fails, so it is refused before anything runs.
LauncherCheck checkLauncher(const Installation& flow, const Launcher& launcher);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_LAUNCHER_HPP
