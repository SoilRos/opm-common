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

#ifndef OPM_RUNFLOW_LAUNCH_PLAN_HPP
#define OPM_RUNFLOW_LAUNCH_PLAN_HPP

#include <opm/runflow/versions/Installation.hpp>
#include <opm/runflow/launch/LaunchInfo.hpp>
#include <opm/runflow/launch/Launcher.hpp>
#include <opm/runflow/submit/JobSpec.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// One run of Flow as a user asks for it.
struct LaunchRequest
{
    std::string deck;                     ///< the deck, relative to where Flow runs, e.g. CASE.DATA
    int ranks = 1;                        ///< MPI ranks of Flow (of the master, if coupled)
    std::vector<std::string> flowOptions; ///< after the deck, as given
};

/// How to start it: the parts of a JobSpec that depend on Flow and the deck.
struct LaunchPlan
{
    std::vector<std::string> command;     ///< Flow, through the MPI launcher if needed
    std::vector<EnvSetting> environment;  ///< PYTHONPATH of the version, if it has one
    int ranks = 1;                        ///< started by the launcher
    int slots = 1;                        ///< to reserve: ranks + the slaves' processes
    std::vector<std::string> banner;      ///< what runs, for the first lines of the log
};

/// Whether a run needs MPI: for more than one rank, and always for a coupled
/// deck (its slaves are spawned through MPI). One rank of an uncoupled deck
/// runs Flow alone, as the legacy script does.
bool needsMpi(const LaunchRequest& request, const LaunchInfo& info);

/// Plans a run, with `launcher` (findLauncher()) when it needs MPI. Throws
/// LaunchError for fewer than one rank, or when the run needs MPI and there
/// is no launcher.
LaunchPlan planLaunch(const Installation& flow, const LaunchRequest& request,
                      const LaunchInfo& info, const std::optional<Launcher>& launcher);

/// The launch information of a deck and where it came from.
struct DeckResources
{
    LaunchInfo info;
    std::string source; ///< "opmlaunchinfo PATH", or "runflow's parser"
};

/// The version's own opmlaunchinfo, in this order: the configured one (also
/// if it does not exist: deckResources() then says so); an
/// opmlaunchinfo in the directory of each path the version's flow was found
/// as; one next to its canonical path. Nothing if there is none.
std::optional<std::filesystem::path> findLaunchInfo(const Installation& flow);

/// Reads what `deck` needs with the version's own opmlaunchinfo
/// (findLaunchInfo()), which parses it as that version's Flow does; without
/// one (versions released before it, build trees, renamed copies) it is read
/// with this program's parser. Throws LaunchError if the deck cannot be read,
/// with the reader's message.
DeckResources deckResources(const Installation& flow, const std::filesystem::path& deck);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_LAUNCH_PLAN_HPP
