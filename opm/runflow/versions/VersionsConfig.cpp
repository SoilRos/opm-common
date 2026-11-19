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

#include <opm/runflow/versions/VersionsConfig.hpp>

#include <opm/json/JsonObject.hpp>
#include <opm/runflow/config/ConfigFiles.hpp>

#include <stdexcept>

namespace {

namespace fs = std::filesystem;

[[noreturn]] void typeError(const fs::path& origin, const std::string& setting,
                            const std::string& expected)
{
    throw std::runtime_error(origin.string() + ": \"" + setting + "\" must be " + expected);
}

Opm::RunFlow::VersionEntry entryOf(const Json::JsonObject& item, const fs::path& origin,
                                   const std::string& name, std::vector<std::string>& warnings)
{
    const auto setting = "versions." + name;
    if (!item.is_object())
        typeError(origin, setting, "an object");
    if (!item.has_item("flow"))
        throw std::runtime_error(origin.string() + ": \"" + setting + "\" needs \"flow\"");

    const auto base = origin.parent_path();
    Opm::RunFlow::VersionEntry entry;
    entry.origin = origin;
    for (const auto& key : item.keys()) {
        const auto value = item.get_item(key);
        const auto field = setting + "." + key;
        if (key == "flow")
            entry.flow = Opm::RunFlow::resolveProgram(Opm::RunFlow::readString(value, origin, field), base);
        else if (key == "launchinfo")
            entry.launchInfo = Opm::RunFlow::resolveProgram(Opm::RunFlow::readString(value, origin, field), base);
        else if (key == "launcher")
            entry.launcher = Opm::RunFlow::resolveProgram(Opm::RunFlow::readString(value, origin, field), base);
        else if (key == "pythonpath") {
            for (const auto& dir : Opm::RunFlow::readStringList(value, origin, field))
                entry.pythonPath.push_back(Opm::RunFlow::resolveConfigPath(dir, base));
        } else
            warnings.push_back(origin.string() + ": unknown setting \"" + field + "\"");
    }
    return entry;
}

// A list of patterns from one file, with "@inherit" for the earlier list;
// each a path relative to the file.
std::vector<fs::path> patternsOf(const Json::JsonObject& value, const std::vector<fs::path>& earlier,
                                 const fs::path& origin, const std::string& setting,
                                 std::vector<std::string>& warnings)
{
    return Opm::RunFlow::listWithInherited(
        Opm::RunFlow::readStringList(value, origin, setting), earlier,
        [&](const std::string& pattern) { return Opm::RunFlow::resolveConfigPath(pattern, origin.parent_path()); },
        setting, origin, warnings);
}

} // namespace

namespace Opm::RunFlow {

void SearchSettings::merge(const Json::JsonObject& section, const fs::path& origin,
                           std::vector<std::string>& warnings)
{
    if (!section.is_object())
        typeError(origin, "search", "an object");
    for (const auto& key : section.keys()) {
        const auto value = section.get_item(key);
        if (key == "flow")
            flow = patternsOf(value, flow, origin, "search.flow", warnings);
        else if (key == "pythonpath")
            pythonPath = patternsOf(value, pythonPath, origin, "search.pythonpath", warnings);
        else
            warnings.push_back(origin.string() + ": unknown setting \"search." + key + "\"");
    }
}

void VersionsConfig::setDefaultVersion(const Json::JsonObject& value, const fs::path& origin)
{
    defaultVersion = readString(value, origin, "default_version");
}

void VersionsConfig::mergeVersions(const Json::JsonObject& value, const fs::path& origin,
                                   std::vector<std::string>& warnings)
{
    if (!value.is_object())
        typeError(origin, "versions", "an object of named versions");
    for (const auto& name : value.keys())
        versions[name] = entryOf(value.get_item(name), origin, name, warnings); // the whole entry
}

} // namespace Opm::RunFlow
