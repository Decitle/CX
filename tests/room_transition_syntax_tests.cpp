#include "cx/script.hpp"
#include <iostream>
#include <string>

int main() {
    const std::string source = R"CX(
enter room "cave" at (5, 3)
go to room "forest" at (10 + 2, 4)
switch to room "village" at {village_spawn}
set {cave_spawn} to an entrance point at (1, 2)
enter room "cave" at {cave_spawn}
)CX";
    const auto program = cx::ScriptParser{}.parseText(source);
    if (!program.diagnostics.empty()) {
        for (const auto& d : program.diagnostics)
            std::cerr << d.message << " suggestion: " << d.suggestion << '\n';
        std::cerr << "Room transition syntax should parse all direct and named entrances.\n";
        return 1;
    }
    if (program.bootstrap.size() != 5) {
        std::cerr << "Expected five room-related statements, got " << program.bootstrap.size() << ".\n";
        return 1;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (program.bootstrap[i].type != cx::StatementType::EnterRoom) {
            std::cerr << "Natural room transition alias did not map to EnterRoom.\n";
            return 1;
        }
    }
    if (program.bootstrap[0].expression != "(5, 3)" ||
        program.bootstrap[1].expression != "(10 + 2, 4)" ||
        program.bootstrap[2].expression != "{village_spawn}" ||
        program.bootstrap[4].expression != "{cave_spawn}") {
        std::cerr << "Room entry operands should be preserved for runtime evaluation.\n";
        return 1;
    }
    std::cout << "CX_ROOM_TRANSITION_SYNTAX_TESTS_OK\n";
    return 0;
}
