#pragma once

#include "model.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>

#include <vector>

namespace dd {

    // What a C++ type means for Lua (plan section 4): kind, nullability and the types inside it.
    TypeInfo classifyType(clang::QualType type, const clang::ASTContext& context);

    // The values a function with this C++ return type gives to Lua: none for void, one per member of a tuple or
    // pair, otherwise the type itself.
    std::vector<TypeInfo> classifyReturns(clang::QualType type, const clang::ASTContext& context);

} // namespace dd
