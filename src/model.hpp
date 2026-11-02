#pragma once

#include <compare>
#include <string>

namespace dd {

    // Where an extracted entity is written, so readers can jump to the binding and outputs stay comparable.
    struct SourceLoc {
        // Path relative to the --root directory, with '/' separators on every platform.
        std::string file;
        // 1-based line of the entity's first token (after macro expansion to a file location).
        unsigned line = 0;
        // 1-based column, to tell several bindings on one line apart.
        unsigned column = 0;

        auto operator<=>(const SourceLoc&) const = default;
    };

} // namespace dd
