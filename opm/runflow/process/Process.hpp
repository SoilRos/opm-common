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

#ifndef OPM_RUNFLOW_PROCESS_HPP
#define OPM_RUNFLOW_PROCESS_HPP

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// The outcome of a program run by captureOutput().
struct CapturedOutput
{
    int exitCode = -1;     ///< the exit status; -1 if it did not exit normally
    bool timedOut = false; ///< killed because it took longer than allowed
    std::string output;    ///< what it wrote to standard output
    std::string errors;    ///< what it wrote to standard error
};

/// Runs a program and returns what it wrote. `input` is its standard input
/// (empty: /dev/null). argv[0] without a '/' is searched for in PATH.
/// A program still running after `timeout` is killed.
/// Throws std::system_error if the program cannot be started.
CapturedOutput captureOutput(const std::vector<std::string>& argv,
                             std::chrono::milliseconds timeout,
                             const std::string& input = {});

/// A program run in the foreground, whose output goes to the terminal and,
/// at the same time, to files (as `cmd > >(tee out) 2> >(tee err >&2)`).
struct ForegroundCommand
{
    std::vector<std::string> argv;        ///< argv[0] is searched in the PATH of `environment`
    std::filesystem::path workdir;        ///< where it runs
    std::vector<std::string> environment; ///< its whole environment, "NAME=value"
    std::string banner;                   ///< written first, to the terminal and stdoutFile
    std::filesystem::path stdoutFile;     ///< a copy of standard output (empty: none); overwritten
    std::filesystem::path stderrFile;     ///< a copy of standard error (empty: none; may be stdoutFile)
    int terminalOut = 1;                  ///< where standard output is shown
    int terminalErr = 2;                  ///< where standard error is shown
};

/// Runs `command` until it ends and returns its exit status; 128 + N if it was
/// killed by signal N, as a shell reports it; 127 if it cannot be started.
/// While it runs, this process ignores SIGINT and SIGQUIT (as system() does:
/// the program gets them from the terminal and decides), and passes SIGTERM
/// on to it. Output is copied until the program closes it, and for at most
/// 2 seconds after the program exited (a process it left behind may keep the
/// output open). Throws std::system_error if a file cannot be opened.
///
/// Invariant: the calling program is single-threaded (it uses fork()).
int runForeground(const ForegroundCommand& command);

/// The environment of this process, as "NAME=value" strings.
std::vector<std::string> currentEnvironment();

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_PROCESS_HPP
