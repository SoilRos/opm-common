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

#ifndef OPM_RUNFLOW_RUN_LOG_HPP
#define OPM_RUNFLOW_RUN_LOG_HPP

#include <filesystem>
#include <string>

namespace Opm::RunFlow {

/// What one run of runflow did, for its run log.
struct RunRecord
{
    std::string runflowVersion;   ///< e.g. "2026.10 (8d562c844)"
    std::string flowVersion;      ///< e.g. "2026.04 Release", or "(version unknown)"
    std::filesystem::path flow;   ///< the executable
    std::string where;            ///< "here", or "LSF"
    std::string command;          ///< what was run: Flow's command, or the bsub line
    int exitStatus = 0;           ///< Flow's (here) or bsub's (LSF)
};

/// One entry of the log, e.g.
///
///   2026-11-24 14:02:11 CET  user@host  runflow 2026.10 (8d562c844)
///     flow:    2026.04 Release  /opt/opm/2026.04/bin/flow
///     where:   LSF
///     command: cd /data/case && bsub -n 6 -q daily ... < CASE.job
///     status:  0 (bsub's)
std::string formatRunRecord(const RunRecord& record, const std::string& date,
                            const std::string& user, const std::string& host);

/// Appends `record` to `file`, with the current date, user and host. The log
/// only records: a file that cannot be written is ignored.
void appendRunLog(const std::filesystem::path& file, const RunRecord& record);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_RUN_LOG_HPP
