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

// opmlaunchinfo: prints what a deck needs to be launched, as JSON. Each OPM
// version installs its own, so that runflow reads a deck with the parser of the
// Flow that will run it. The output is a contract between versions: see
// opm/runflow/launch/LaunchInfo.hpp.

#include <opm/runflow/launch/LaunchInfo.hpp>

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const std::string usage = "usage: opmlaunchinfo DECK\n"
                              "Prints what DECK needs to be launched (reservoir coupling slaves), "
                              "as JSON. See opmlaunchinfo(1).\n";
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cout << usage;
        return 0;
    }
    if (argc != 2 || std::string(argv[1]).starts_with("-")) {
        std::cerr << usage;
        return 2;
    }
    try {
        std::cout << Opm::RunFlow::readLaunchInfo(argv[1]).toJson() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "opmlaunchinfo: " << e.what() << '\n';
        return 1;
    }
}
