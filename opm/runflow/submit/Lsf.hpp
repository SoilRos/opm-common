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

#ifndef OPM_RUNFLOW_LSF_HPP
#define OPM_RUNFLOW_LSF_HPP

#include <opm/runflow/submit/JobSpec.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Json { class JsonObject; }

namespace Opm::RunFlow {

/// The site's LSF settings: the "lsf" section of runflow.json.
///
///   "lsf": { "bsub": "/global/bin/bsub", "bsub_options": ["-P", "reservoir"],
///            "default_queue": "daily", "single_host": true, "max_slots_per_host": 128,
///            "launcher": "mpirun",
///            "launcher_options": ["--bind-to", "none", "-np", "{np}", "-x", "OMP_NUM_THREADS=1"] }
///
/// Programs (bsub, launcher) follow resolveProgram(); the launcher starts MPI
/// runs inside jobs (findLauncher()). The defaults are the legacy runflow
/// script's.
struct LsfSettings
{
    std::string bsub = "bsub";             ///< a name is found in PATH (by the shell)
    std::vector<std::string> bsubOptions;  ///< added to every bsub
    std::string defaultQueue;              ///< for jobs without -q (empty: LSF's default)
    bool singleHost = true;                ///< all of a job's slots on one host (-R 'span[hosts=1]')
    std::optional<int> maxSlotsPerHost;    ///< if set, single-host jobs needing more are refused
    std::string launcher = "mpirun";
    std::vector<std::string> launcherOptions{"--bind-to", "none", "-np", "{np}",
                                             "-x", "OMP_NUM_THREADS=1"};

    /// Merges one file's "lsf" section by name: each setting it gives
    /// replaces the earlier one; in a list (bsub_options, launcher_options),
    /// "@inherit" stands for the earlier list. Settings it does not give are
    /// kept. Unknown settings warn; wrong types throw std::runtime_error
    /// naming `origin`.
    void merge(const Json::JsonObject& section, const std::filesystem::path& origin,
               std::vector<std::string>& warnings);
};

/// The shell command line that submits `job` to LSF, every word quoted, run
/// in the job's directory (the order of the legacy runflow script):
///
///   cd WORKDIR && bsub -n SLOTS -q QUEUE -J NAME -R 'span[hosts=1]'
///                     -o REPORT OPTIONS... < SCRIPTFILE
///
/// "{id}" in the report is LSF's %J. The script goes on standard input, so
/// LSF keeps its own copy. Throws SubmitError if a single-host job needs more
/// slots than max_slots_per_host, or if the job has no script file.
std::string lsfCommand(const JobSpec& job, const LsfSettings& settings);

/// The job script for LSF: jobScript(), with LSB_JOBID as the job's ID.
std::string lsfJobScript(const JobSpec& job);

/// Submits `job`: writes its script to job.scriptFile, then runs
/// lsfCommand() with system(), so that bsub's answer goes to the terminal as
/// it is. Returns bsub's exit status (127: bsub not found).
int submitToLsf(const JobSpec& job, const LsfSettings& settings);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_LSF_HPP
