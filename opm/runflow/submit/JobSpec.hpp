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

#ifndef OPM_RUNFLOW_JOB_SPEC_HPP
#define OPM_RUNFLOW_JOB_SPEC_HPP

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// A change to one environment variable of a run.
struct EnvSetting
{
    enum class Mode {
        Set,     ///< NAME=value
        Prepend, ///< value before the current value, separated by ':' (as PYTHONPATH)
    };
    std::string name;  ///< a valid shell variable name
    std::string value;
    Mode mode = Mode::Set;
};

/// One run of a program, here or as a job: what to run and what it needs.
/// File names are relative to `workdir`.
struct JobSpec
{
    std::vector<std::string> command;     ///< e.g. mpirun ... flow CASE.DATA
    std::filesystem::path workdir;        ///< where it runs; absolute
    std::vector<EnvSetting> environment;  ///< applied in order, over the current environment
    std::string name;                     ///< the job's name, e.g. CASE
    std::string queue;                    ///< empty: the site's default
    int slots = 1;                        ///< processor slots to reserve; may exceed the ranks
    bool singleHost = true;               ///< all slots on one host
    std::string stdoutFile;               ///< written while it runs, e.g. CASE.LOG
    std::string stderrFile;               ///< written while it runs (empty: with stdout)
    std::vector<std::string> banner;      ///< lines written first to stdoutFile
    std::string scriptFile;               ///< a job's script, e.g. CASE.job
    std::string report;                   ///< the scheduler's own report; "{id}" is the job's ID
};

/// A run that cannot be made (e.g. more slots than a host has). The message is
/// for users: it says what was asked and what is available.
class SubmitError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// `word` as one shell word: in single quotes, unless it needs none.
std::string shellQuote(const std::string& word);

/// The words of a command, each quoted, separated by spaces.
std::string shellQuote(const std::vector<std::string>& words);

/// The bash script that runs `job` as a scheduler's job:
///
///   #!/bin/bash
///   cd WORKDIR || exit 1
///   export NAME=value                           (each EnvSetting)
///   exec > STDOUT 2> STDERR                     (written while it runs)
///   echo "SCHEDULER job $ID_VARIABLE on HOST, started DATE"
///   printf '%s\n' BANNER...
///   exec COMMAND                                (its exit status is the job's)
///
/// `scheduler` names it in the first output line, and `idVariable` is the
/// variable holding the job's ID inside the job (LSB_JOBID for LSF).
std::string jobScript(const JobSpec& job, const std::string& scheduler,
                      const std::string& idVariable);

/// `base` ("NAME=value" strings) with the job's environment settings applied.
std::vector<std::string> applyEnvironment(std::vector<std::string> base,
                                          const std::vector<EnvSetting>& settings);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_JOB_SPEC_HPP
