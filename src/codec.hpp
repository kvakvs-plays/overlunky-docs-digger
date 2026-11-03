#pragma once

#include "model.hpp"

#include <llvm/ADT/StringRef.h>

#include <string>

// Lossless JSON of one translation unit's partial extraction, internal fields included, for the result cache
// (cache.hpp). Unlike the toJSON() output writers it keeps every field and the map keys, so a decoded extraction
// merges exactly like a freshly parsed one.
namespace dd {

    // The partial extraction as compact JSON text.
    std::string encodeExtraction(const Extraction& extraction);

    // Reads encodeExtraction's text into out; false when the text does not fit the model (a damaged cache row).
    bool decodeExtraction(llvm::StringRef text, Extraction& out);

} // namespace dd
