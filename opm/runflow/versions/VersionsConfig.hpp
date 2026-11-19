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

#ifndef OPM_RUNFLOW_VERSIONS_CONFIG_HPP
#define OPM_RUNFLOW_VERSIONS_CONFIG_HPP

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Json { class JsonObject; }

namespace Opm::RunFlow {

/// A named version in runflow.json: "versions": { NAME: { ... } }.
///
/// Program fields (flow, launchinfo, launcher) follow resolveProgram(): a
/// value with a '/' is a path, relative to the file; a bare name is found
/// when used.
struct VersionEntry
{
    std::string flow;                       ///< the executable
    std::optional<std::string> launchInfo;  ///< its opmlaunchinfo, if not next to flow
    std::optional<std::string> launcher;    ///< its MPI launcher program, see findLauncher()
    std::vector<std::filesystem::path> pythonPath; ///< put before PYTHONPATH when it runs
    std::filesystem::path origin;           ///< the file it is from
};

/// Where versions not named in "versions" are found: the "search" section.
///
///   "search": {
///     "flow":       ["/project/res/bin/flow*"],
///     "pythonpath": ["/project/res/lib/python3.*/site-packages/{name}"] }
///
/// Its fields mirror those of a version: each gives patterns to find what a
/// version entry would name. Patterns are paths (relative to the file) with
/// *, ? and [...] matched against existing files, as in the shell.
struct SearchSettings
{
    /// Every executable these patterns match is a version (findCandidates()).
    std::vector<std::filesystem::path> flow;
    /// The PYTHONPATH of versions without their own, per version: "{name}"
    /// is the file name it was found as (expandPythonPath()).
    std::vector<std::filesystem::path> pythonPath;

    /// Merges one file's "search" section by name: each list it gives
    /// replaces the earlier one, where "@inherit" stands for the earlier list.
    void merge(const Json::JsonObject& section, const std::filesystem::path& origin,
               std::vector<std::string>& warnings);
};

/// The version settings of runflow.json: "default_version", "search" and
/// "versions". See RunflowConfig for how files combine.
struct VersionsConfig
{
    std::optional<std::string> defaultVersion;
    SearchSettings search;
    std::map<std::string, VersionEntry> versions;

    /// "default_version": the file's value replaces the earlier one.
    void setDefaultVersion(const Json::JsonObject& value, const std::filesystem::path& origin);

    /// "versions": merge by name. Each version the file names is added, or
    /// replaces the whole entry of that name: its fields describe one
    /// executable together. Versions the file does not name are kept.
    /// Unknown fields of an entry go to `warnings`.
    void mergeVersions(const Json::JsonObject& value, const std::filesystem::path& origin,
                       std::vector<std::string>& warnings);
};

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_VERSIONS_CONFIG_HPP
