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

#ifndef OPM_RUNFLOW_BUILD_INFO_HPP
#define OPM_RUNFLOW_BUILD_INFO_HPP

#include <opm/runflow/versions/Installation.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace Opm::RunFlow {

/// The output of `flow --build-info`: one key=value per line, e.g.
///
///   version=2026.04
///   hash=1b2958d6e
///   build_time=2026-04-15 at 10:12:00 hrs
///   build_type=Release
///   mpi=Open MPI v5.0.7
///
/// Unknown keys are ignored (keys may be added). Nothing if there is no
/// version= line: then it is not that output.
std::optional<BuildInfo> parseBuildInfo(const std::string& output);

/// The OPM version in the output of `flow --version`, e.g. "2026.04" from
/// "flow 2026.04": the second word of the first line.
std::optional<std::string> parseFlowVersion(const std::string& output);

/// Asks a Flow binary about itself: `flow --build-info`, and for versions
/// without it, `flow --version` (the OPM version only). Each for at most 30
/// seconds. Everything empty if neither answers.
BuildInfo describeFlow(const std::filesystem::path& flow);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_BUILD_INFO_HPP
