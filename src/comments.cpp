#include "comments.hpp"

#include <clang/AST/RawCommentList.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/Support/Regex.h>

#include <map>

namespace dd {

    namespace {

        // Text lines of a raw comment without the comment markers.
        std::vector<std::string> commentLines(const clang::RawComment& comment, const clang::ASTContext& context) {
            std::vector<std::string> lines;
            for (const clang::RawComment::CommentLine& line :
                 comment.getFormattedLines(context.getSourceManager(), context.getDiagnostics())) {
                lines.push_back(line.Text);
            }
            return lines;
        }

        // Line number of loc in its file.
        unsigned lineOf(clang::SourceLocation loc, const clang::SourceManager& sm) {
            return sm.getExpansionLineNumber(loc);
        }

        // The text of the given 1-based line of file, without the line break.
        llvm::StringRef lineText(clang::FileID file, unsigned line, const clang::SourceManager& sm) {
            const clang::SourceLocation start = sm.translateLineCol(file, line, 1);
            const llvm::StringRef rest = sm.getBufferData(file).substr(sm.getFileOffset(start));
            return rest.take_until([](char c) { return c == '\n' || c == '\r'; });
        }

        // How much c changes the bracket depth: +1 for <([, -1 for >)], 0 otherwise.
        int nestingChange(char c) {
            if (c == '<' || c == '(' || c == '[') {
                return 1;
            }
            return (c == '>' || c == ')' || c == ']') ? -1 : 0;
        }

        // Splits at commas outside <>, () and [] (template arguments and nested signatures keep their commas).
        std::vector<std::string> splitTopLevel(llvm::StringRef text) {
            std::vector<std::string> parts;
            int depth = 0;
            size_t start = 0;
            for (size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                depth += nestingChange(c);
                if (c == ',' && depth == 0) {
                    parts.push_back(text.slice(start, i).trim().str());
                    start = i + 1;
                }
            }
            if (!text.trim().empty()) {
                parts.push_back(text.substr(start).trim().str());
            }
            return parts;
        }

        // `optional<int> depth` → name `depth`, type `optional<int>`; a lone type keeps an empty name.
        Param paramFromText(llvm::StringRef text) {
            const size_t nameStart =
                text.find_last_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
            Param param;
            if (nameStart == llvm::StringRef::npos || nameStart + 1 == text.size()) {
                param.type.spelling = text.str();
                return param;
            }
            param.name = text.substr(nameStart + 1).str();
            param.type.spelling = text.take_front(nameStart + 1).trim().str();
            return param;
        }

    } // namespace

    Doc parseDoc(std::vector<std::string> lines) {
        Doc doc;
        while (!lines.empty()) {
            const llvm::StringRef first = llvm::StringRef(lines.front()).trim();
            if (first.starts_with("NoDoc")) {
                doc.nodoc = true;
            }
            else if (first == "Deprecated") {
                doc.deprecated = true;
            }
            else {
                break;
            }
            lines.erase(lines.begin());
        }
        doc.lines = std::move(lines);
        return doc;
    }

    Doc docBefore(clang::SourceLocation loc, const clang::ASTContext& context) {
        const clang::SourceManager& sm = context.getSourceManager();
        const auto [file, offset] = sm.getDecomposedLoc(sm.getFileLoc(loc));
        const std::map<unsigned, clang::RawComment*>* comments = context.Comments.getCommentsInFile(file);
        if (comments == nullptr) {
            return {};
        }
        auto it = comments->lower_bound(offset);
        if (it == comments->begin()) {
            return {};
        }
        const clang::RawComment& comment = *std::prev(it)->second;
        if (lineOf(comment.getEndLoc(), sm) + 1 != lineOf(sm.getFileLoc(loc), sm)) {
            return {};
        }
        return parseDoc(commentLines(comment, context));
    }

    Doc docOfDecl(const clang::Decl& decl, const clang::ASTContext& context) {
        const clang::RawComment* comment = context.getRawCommentForAnyRedecl(&decl);
        return comment == nullptr ? Doc{} : parseDoc(commentLines(*comment, context));
    }

    std::vector<CommentedBinding> commentedBindings(clang::FileID file, const clang::ASTContext& context) {
        const clang::SourceManager& sm = context.getSourceManager();
        const std::map<unsigned, clang::RawComment*>* comments = context.Comments.getCommentsInFile(file);
        if (comments == nullptr) {
            return {};
        }
        static const llvm::Regex binding(
            R"re(^[[:space:]]*//[[:space:]]*lua\["([^"]+)"\][[:space:]]*=[[:space:]]*(.*)$)re");
        std::vector<CommentedBinding> found;
        for (const auto& [offset, comment] : *comments) {
            const unsigned line = lineOf(comment->getEndLoc(), sm) + 1;
            llvm::SmallVector<llvm::StringRef, 3> groups;
            if (binding.match(lineText(file, line, sm), &groups)) {
                found.push_back({groups[1].str(), groups[2].rtrim(" \t;").str(), sm.translateLineCol(file, line, 1),
                                 parseDoc(commentLines(*comment, context))});
            }
        }
        return found;
    }

    FunctionSig signatureFromText(llvm::StringRef text) {
        FunctionSig sig;
        sig.cppTarget = "doc comment";
        const size_t open = text.find('(', text.find(']'));
        const size_t close = text.find(')', open);
        if (open == llvm::StringRef::npos || close == llvm::StringRef::npos) {
            return sig;
        }
        for (const std::string& part : splitTopLevel(text.slice(open + 1, close))) {
            sig.params.push_back(paramFromText(part));
        }
        llvm::StringRef rest = text.substr(close + 1).trim();
        if (rest.consume_front("->")) {
            const llvm::StringRef returned = rest.take_until([](char c) { return c == '{' || c == ';'; }).trim();
            if (!returned.empty() && returned != "void") {
                TypeInfo type;
                type.spelling = returned.str();
                sig.returns.push_back(std::move(type));
            }
        }
        return sig;
    }

} // namespace dd
