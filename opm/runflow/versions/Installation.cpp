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

#include <opm/runflow/versions/Installation.hpp>

#include <algorithm>

namespace Opm::RunFlow {

void merge(Installation& target, const Installation& other)
{
    const auto addNew = [](auto& to, const auto& from) {
        for (const auto& item : from) {
            if (std::ranges::find(to, item) == to.end())
                to.push_back(item);
        }
    };
    addNew(target.paths, other.paths);
    addNew(target.names, other.names);
    addNew(target.executableNames, other.executableNames);
    addNew(target.foundBy, other.foundBy);
    if (other.launchInfo)
        target.launchInfo = other.launchInfo;
    if (other.launcher)
        target.launcher = other.launcher;
    if (other.pythonPath)
        target.pythonPath = other.pythonPath;
}

} // namespace Opm::RunFlow
