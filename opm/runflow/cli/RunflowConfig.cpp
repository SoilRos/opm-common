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

#include <opm/runflow/cli/RunflowConfig.hpp>

#include <opm/json/JsonObject.hpp>

#include <memory>
#include <stdexcept>
#include <system_error>

namespace Opm::RunFlow {

void RunflowConfig::read(const Json::JsonObject& file, const std::filesystem::path& origin,
                         std::vector<std::string>& warnings)
{
    const auto error = [&](const std::string& what) {
        throw std::runtime_error(origin.string() + ": " + what);
    };
    if (!file.is_object())
        error("must be a JSON object");
    for (const auto& key : file.keys()) {
        const auto value = file.get_item(key);
        if (key == "default_version") {          // a value: replace
            versions.setDefaultVersion(value, origin);
        } else if (key == "scheduler") {         // a value: replace
            scheduler = readString(value, origin, key);
            if (scheduler != "lsf")
                error("\"scheduler\": \"" + scheduler + "\" is not supported (yet); use \"lsf\"");
        } else if (key == "search") {            // an object: merge by key
            versions.search.merge(value, origin, warnings);
        } else if (key == "versions") {          // merge by name, each entry whole
            versions.mergeVersions(value, origin, warnings);
        } else if (key == "lsf") {               // merge by name
            lsf.merge(value, origin, warnings);
        } else if (key == "local") {             // merge by name
            local.merge(value, origin, warnings);
        } else {
            warnings.push_back(origin.string() + ": unknown setting \"" + key + "\"");
        }
    }
}

RunflowConfig RunflowConfig::load(const std::vector<ConfigFile>& files,
                                  std::vector<std::string>& warnings)
{
    RunflowConfig config;
    for (const auto& file : files) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file.path, ec)) {
            if (file.required)
                throw std::runtime_error(file.path.string() + ": no such file (RUNFLOW_CONFIG)");
            continue;
        }
        std::unique_ptr<Json::JsonObject> json; // JsonObject must not be copied
        try {
            json = std::make_unique<Json::JsonObject>(file.path);
        } catch (const std::invalid_argument&) {
            throw std::runtime_error(file.path.string() + ": not valid JSON");
        }
        config.read(*json, file.path, warnings);
    }
    return config;
}

} // namespace Opm::RunFlow
