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

#ifndef OPM_RUNFLOW_COMMAND_LINE_HPP
#define OPM_RUNFLOW_COMMAND_LINE_HPP

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// Wrong options or arguments (runflow exits with status 2).
class UsageError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// runflow's command line: the legacy runflow script's options, plus --dry-run.
///
///   runflow [-q QUEUE] [-i] [-l] [-v VERSION] [-n NP] [-o "OPTIONS"] [--dry-run] DECK
struct CommandLine
{
    std::optional<std::string> queue;   ///< -q, --queue
    bool interactive = false;           ///< -i, --interactive: run here, not as a job
    bool list = false;                  ///< -l, --list
    std::optional<std::string> version; ///< -v, --version
    int processes = 1;                  ///< -n, --np
    std::string flowOptions;            ///< -o, --op: as given, split by splitFlowOptions()
    bool help = false;                  ///< -h, --help
    bool dryRun = false;                ///< --dry-run
    std::vector<std::string> decks;     ///< the arguments that are not options
};

/// Parses the arguments (args[0] is the program) as the legacy script's GNU
/// `getopt -a` did: long options may start with one dash (-np, -op, -queue),
/// unambiguous prefixes are accepted (--inter), and options may come after the
/// deck. A later option replaces an earlier one. Throws UsageError for an
/// unknown option, a missing value, or a number of processes below 1.
CommandLine parseCommandLine(const std::vector<std::string>& args);

/// The words of -o: split at spaces, tabs and newlines, as the legacy script's
/// unquoted ${options} was. (Unlike the shell, '*' and '?' are not expanded.)
std::vector<std::string> splitFlowOptions(const std::string& options);

/// Where and on what Flow runs, by the legacy script's rule: in the deck's
/// directory, on ROOT.DATA, where ROOT is the deck's file name up to its first
/// dot. "cases/A.DATA", "cases/A" and "cases/A.B.DATA" all run cases/A.DATA.
struct DeckLocation
{
    std::filesystem::path workdir; ///< the deck's directory, absolute
    std::string root;              ///< e.g. "A": names the job and its files
    std::string deck;              ///< ROOT.DATA, relative to workdir
};

DeckLocation locateDeck(const std::string& argument);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_COMMAND_LINE_HPP
