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

#ifndef OPM_RUNFLOW_LAUNCH_INFO_HPP
#define OPM_RUNFLOW_LAUNCH_INFO_HPP

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// The newest launch information schema this program reads and writes. As
/// for manifests: fields are only ever added, readers ignore fields they do
/// not know, and a newer schema is refused.
inline constexpr int launchInfoSchema = 1;

/// A deck that cannot be read, or launch information that cannot be used.
class LaunchError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// A slave of a reservoir coupling master (one record of SLAVES), as Flow
/// reads it: Flow spawns `processes` ranks running DIRECTORY/DECK.
struct SlaveInfo
{
    std::string name;      ///< SLAVE_RESERVOIR
    std::string deck;      ///< SLAVE_ECLBASE
    std::string directory; ///< DIRECTORY, as written (relative to where Flow runs)
    int processes = 1;     ///< NUM_PE
};

/// What a deck needs to be launched; the output of opmlaunchinfo:
///
///   { "schema": 1,
///     "slaves": [ { "name": "RES-1", "deck": "RC-01_MOD1_HIST",
///                   "directory": "../mod1", "num_pe": 4 } ] }
struct LaunchInfo
{
    std::vector<SlaveInfo> slaves; ///< empty: not a coupling master

    /// Sum of the slaves' processes: the slots a run needs besides its own ranks.
    int slaveProcesses() const;

    /// As JSON, in the format above.
    std::string toJson() const;

    /// Reads the JSON above. Throws LaunchError if it is not valid, or of a
    /// newer schema.
    static LaunchInfo fromJson(const std::string& json);
};

/// Reads the launch information of a deck with this opm-common's parser, as
/// the Flow of the same version reads it: the same parse settings as Flow's
/// default (not strict), and the same refusals (SLAVES twice, NUM_PE < 1).
/// Reads only RUNSPEC and SCHEDULE when the deck allows it, else the whole
/// deck. Throws LaunchError with the parser's message if the deck cannot be read.
LaunchInfo readLaunchInfo(const std::filesystem::path& deck);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_LAUNCH_INFO_HPP
