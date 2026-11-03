#pragma once

#include "cache.hpp"
#include "model.hpp"

#include <clang/Tooling/CompilationDatabase.h>

#include <string>
#include <vector>

// Parsing the selected translation units: each into its own partial extraction, from the result cache when its files
// are unchanged, the others in parallel; the partial results are merged in source order (plan DIG-4b).
namespace dd {

    // What a run parses and how.
    struct RunSettings {
        // Translation units, in the order their results are merged.
        std::vector<std::string> sources;
        // Arguments appended to every compile command.
        std::vector<std::string> extraArgs;
        // The --root directory: results are reported relative to it.
        std::string root;
        // The result cache, or nullptr to parse everything.
        ResultCache* cache = nullptr;
        // Take results from the cache (false: parse everything, but still store the new results).
        bool readCache = true;
        // Identity of this digger build (hash of the executable): results of other builds are not reused.
        std::string version;
        // Translation units parsed at the same time.
        unsigned jobs = 1;
    };

    // Parses (or takes from the cache) every source and merges the results into out; returns how many translation
    // units had errors (their partial results are merged too, but not cached). Prints a progress line per unit and a
    // heartbeat every 10 s while units are being parsed.
    unsigned runAll(const RunSettings& settings, const clang::tooling::CompilationDatabase& database,
                    FileHashes& hashes, Extraction& out);

} // namespace dd
