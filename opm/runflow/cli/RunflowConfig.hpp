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

#ifndef OPM_RUNFLOW_RUNFLOW_CONFIG_HPP
#define OPM_RUNFLOW_RUNFLOW_CONFIG_HPP

#include <opm/runflow/config/ConfigFiles.hpp>
#include <opm/runflow/submit/Local.hpp>
#include <opm/runflow/submit/Lsf.hpp>
#include <opm/runflow/versions/VersionsConfig.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace Json { class JsonObject; }

namespace Opm::RunFlow {

/// runflow.json: the settings of runflow and flowversions.
///
///   { "default_version": "flowrc47",
///     "scheduler": "lsf",
///     "search": { "flow": ["/project/res/bin/flow*"],
///                 "pythonpath": ["/project/res/lib/python3.*/site-packages/{name}"] },
///     "versions": { "dev": { "flow": "~/opm/build/bin/flow", "launcher": "/opt/openmpi-5/bin/mpirun",
///                            "pythonpath": ["~/opm/build/python"] } },
///     "local": { "launcher": "mpirun", "launcher_options": ["--bind-to", "none", "-np", "{np}"] },
///     "lsf": { "bsub": "/global/bin/bsub", "bsub_options": ["-P", "x"], "default_queue": "daily",
///              "single_host": true, "launcher": "mpirun", "launcher_options": [...] } }
///
/// How files combine. The files of configFiles() are read in order; a
/// setting a file does not give stays as the earlier files had it, and:
///   - objects (the file, "search", "versions", "local", "lsf") merge by key;
///   - values replace the earlier value;
///   - lists replace the earlier list, where an "@inherit" entry stands for
///     the earlier list (inheritMarker);
///   - one exception: a version entry replaces the earlier entry of that name
///     whole, since its fields describe one executable together.
///
/// Paths: a program (flow, launchinfo, launcher, bsub) with a '/' is a path,
/// and a bare name is found when used (resolveProgram()); other paths and
/// patterns are relative to their file.
struct RunflowConfig
{
    VersionsConfig versions;  ///< default_version, search, versions
    std::string scheduler = "lsf"; ///< where jobs go (runs without -i); only "lsf" for now
    LocalSettings local;      ///< runs here (-i)
    LsfSettings lsf;          ///< LSF jobs

    /// Reads one file over the settings so far, as described above. Unknown
    /// settings warn (a newer runflow may know them); wrong types throw
    /// std::runtime_error naming `origin`.
    void read(const Json::JsonObject& file, const std::filesystem::path& origin,
              std::vector<std::string>& warnings);

    /// Reads `files` in order. A missing file is skipped, unless it is
    /// required; a file that is not valid JSON is an error.
    static RunflowConfig load(const std::vector<ConfigFile>& files,
                              std::vector<std::string>& warnings);
};

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_RUNFLOW_CONFIG_HPP
