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

#ifndef OPM_RUNFLOW_LOCAL_HPP
#define OPM_RUNFLOW_LOCAL_HPP

#include <opm/runflow/submit/JobSpec.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace Json { class JsonObject; }

namespace Opm::RunFlow {

/// Runs here (runflow -i): the "local" section of runflow.json.
///
///   "local": { "launcher": "mpirun", "launcher_options": ["--bind-to", "none", "-np", "{np}"] }
///
/// The launcher starts MPI runs here (findLauncher()); the program follows
/// resolveProgram(). The defaults are the legacy runflow script's.
struct LocalSettings
{
    std::string launcher = "mpirun";
    std::vector<std::string> launcherOptions{"--bind-to", "none", "-np", "{np}"};

    /// Merges one file's "local" section by name: each setting it gives
    /// replaces the earlier one; in launcher_options, "@inherit" stands for the
    /// earlier options. Unknown settings warn; wrong types throw
    /// std::runtime_error naming `origin`.
    void merge(const Json::JsonObject& section, const std::filesystem::path& origin,
               std::vector<std::string>& warnings);
};

/// Throws SubmitError if `job` needs more slots than this machine has cores
/// (`cores`; 0: as many as it has processors).
void checkLocalSlots(const JobSpec& job, unsigned cores = 0);

/// What runLocal() does, in shell terms, for a dry run:
///
///   cd WORKDIR
///   export NAME=value ...
///   COMMAND    # output also to STDOUT and STDERR
std::string describeLocal(const JobSpec& job);

/// Runs `job` here, in the foreground (runflow -i), and returns its exit
/// status. Its output goes to the terminal (`terminalOut`, `terminalErr`) and,
/// as it comes, to the job's stdout and stderr files, after a first line
/// "local run on HOST, started DATE" and the banner. See runForeground() for
/// signals. Throws SubmitError for too many slots (checkLocalSlots()).
int runLocal(const JobSpec& job, unsigned cores = 0, int terminalOut = 1, int terminalErr = 2);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_LOCAL_HPP
