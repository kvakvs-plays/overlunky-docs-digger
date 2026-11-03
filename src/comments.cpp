#include "comments.hpp"

#include <clang/AST/RawCommentList.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/Support/Regex.h>

#include <map>
#include <optional>

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

        // True when only spaces and tabs are between the start of pos's line and pos.
        bool startsLine(llvm::StringRef text, size_t pos) {
            const size_t lineStart = text.find_last_of('\n', pos == 0 ? 0 : pos - 1);
            const size_t from = lineStart == llvm::StringRef::npos ? 0 : lineStart + 1;
            return text.slice(from, pos).trim(" \t").empty();
        }

        // True for a C identifier: the first line of a block that documents an enum or usertype.
        bool isIdentifier(llvm::StringRef text) {
            return !text.empty() &&
                text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "0123456789_") == llvm::StringRef::npos;
        }

        // The block doc in the text between `/*` and `*/`, when its first line is one name.
        std::optional<BlockDoc> blockDocOf(llvm::StringRef body) {
            llvm::SmallVector<llvm::StringRef, 16> lines;
            body.split(lines, '\n');
            BlockDoc block{lines.front().trim().str(), {}};
            if (!isIdentifier(block.target)) {
                return std::nullopt;
            }
            for (llvm::StringRef line : llvm::ArrayRef(lines).drop_front()) {
                line = line.trim();
                line.consume_front("//");
                if (!line.trim().empty()) {
                    block.lines.push_back(line.trim().str());
                }
            }
            return block;
        }

        // `optional<int> depth` → name `depth`, type `optional<int>`; a lone type keeps an empty name. A default
        // value (`string version = ""`) is dropped and makes the parameter omittable.
        Param paramFromText(llvm::StringRef text) {
            Param param;
            if (const size_t equals = text.find('='); equals != llvm::StringRef::npos) {
                text = text.take_front(equals).trim();
                param.omittable = true;
            }
            const size_t nameStart =
                text.find_last_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
            if (nameStart == llvm::StringRef::npos || nameStart + 1 == text.size()) {
                param.type.spelling = text.str();
                return param;
            }
            param.name = text.substr(nameStart + 1).str();
            param.type.spelling = text.take_front(nameStart + 1).trim().str();
            return param;
        }

        // A text-only type (kind Unknown), for signatures written in comments.
        TypeInfo textType(llvm::StringRef text) {
            TypeInfo type;
            type.spelling = text.trim().str();
            return type;
        }

        // One regex match of a prose callback signature: groups 2 to 4 are return, name and parameters.
        ProseCallback proseCallback(llvm::ArrayRef<llvm::StringRef> groups) {
            ProseCallback result{groups[3].str(), {}};
            CallbackSig& sig = result.callback;
            sig.origin = CallbackOrigin::DocComment;
            sig.prose = (groups[2] + " " + groups[3] + "(" + groups[4] + ")").str();
            for (const std::string& part : splitTopLevel(groups[4])) {
                sig.params.push_back(paramFromText(part));
            }
            if (groups[2] != "nil" && groups[2] != "void") {
                sig.returns.push_back(textType(groups[2]));
            }
            return result;
        }

        // Moves a `Group: <name>` line, wherever it is in the comment, out of the text into doc.group.
        void takeGroup(std::vector<std::string>& lines, Doc& doc) {
            std::erase_if(lines, [&doc](const std::string& line) {
                llvm::StringRef text = llvm::StringRef(line).trim();
                if (!text.consume_front("Group:")) {
                    return false;
                }
                doc.group = text.trim().str();
                return true;
            });
        }

    } // namespace

    Doc parseDoc(std::vector<std::string> lines) {
        Doc doc;
        takeGroup(lines, doc);
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
        // Only `///` documents, as in the old parser: a `/**/` ending the line above is not a comment on loc.
        if (lineOf(comment.getEndLoc(), sm) + 1 != lineOf(sm.getFileLoc(loc), sm) ||
            !comment.getRawText(sm).starts_with("///")) {
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

    std::vector<CommentedPair> commentedPairs(clang::SourceRange range, const clang::ASTContext& context) {
        const clang::SourceManager& sm = context.getSourceManager();
        const clang::SourceLocation begin = sm.getFileLoc(range.getBegin());
        const clang::FileID file = sm.getFileID(begin);
        const unsigned last = lineOf(sm.getFileLoc(range.getEnd()), sm);
        std::vector<CommentedPair> found;
        for (unsigned line = lineOf(begin, sm); line <= last; ++line) {
            const llvm::StringRef text = lineText(file, line, sm);
            const size_t marker = text.find("//,");
            const std::vector<std::string> parts =
                marker == llvm::StringRef::npos ? std::vector<std::string>() : splitTopLevel(text.substr(marker + 3));
            if (parts.size() == 2) {
                found.push_back({llvm::StringRef(parts[0]).trim('"').str(), parts[1],
                                 sm.translateLineCol(file, line, static_cast<unsigned>(marker) + 1)});
            }
        }
        return found;
    }

    std::vector<BlockDoc> blockDocs(clang::FileID file, const clang::SourceManager& sm) {
        const llvm::StringRef text = sm.getBufferData(file);
        std::vector<BlockDoc> found;
        size_t pos = text.find("/*");
        while (pos != llvm::StringRef::npos) {
            const size_t end = text.find("*/", pos + 2);
            if (end == llvm::StringRef::npos) {
                break;
            }
            std::optional<BlockDoc> block =
                startsLine(text, pos) ? blockDocOf(text.slice(pos + 2, end)) : std::optional<BlockDoc>();
            if (block) {
                found.push_back(std::move(*block));
            }
            pos = text.find("/*", end + 2);
        }
        return found;
    }

    std::optional<std::string> autodocHint(clang::SourceLocation loc, const clang::ASTContext& context) {
        // Lines looked at above loc; hints sit directly above, with at most a few `///` lines in between.
        constexpr unsigned MaxLines = 6;
        static const llvm::Regex hint(R"re(&[A-Za-z_][A-Za-z0-9_]*::([A-Za-z_][A-Za-z0-9_]*))re");
        const clang::SourceManager& sm = context.getSourceManager();
        const clang::SourceLocation fileLoc = sm.getFileLoc(loc);
        const clang::FileID file = sm.getFileID(fileLoc);
        const unsigned line = lineOf(fileLoc, sm);
        for (unsigned above = line - 1; above >= 1 && above + MaxLines >= line; --above) {
            const llvm::StringRef text = lineText(file, above, sm).trim();
            if (!text.starts_with("//")) {
                return std::nullopt;
            }
            llvm::SmallVector<llvm::StringRef, 2> groups;
            if (!text.starts_with("///") && hint.match(text, &groups)) {
                return groups[1].str();
            }
        }
        return std::nullopt;
    }

    bool commentedBindingAbove(clang::SourceLocation loc, llvm::StringRef key, const clang::ASTContext& context) {
        // Lines looked at above loc, as for autodocHint.
        constexpr unsigned MaxLines = 6;
        const std::string pattern = "[\"" + key.str() + "\"]";
        const clang::SourceManager& sm = context.getSourceManager();
        const clang::SourceLocation fileLoc = sm.getFileLoc(loc);
        const clang::FileID file = sm.getFileID(fileLoc);
        const unsigned line = lineOf(fileLoc, sm);
        for (unsigned above = line - 1; above >= 1 && above + MaxLines >= line; --above) {
            const llvm::StringRef text = lineText(file, above, sm).trim();
            if (!text.starts_with("//")) {
                return false;
            }
            if (!text.starts_with("///") && text.contains(pattern) && text.contains('=')) {
                return true;
            }
        }
        return false;
    }

    std::vector<ProseCallback> proseCallbacks(const std::vector<std::string>& lines) {
        static const llvm::Regex prose(R"re((signature[^`]* is|function) `?([^ `]+) ([A-Za-z0-9_]*)\(([^)]*)\))re");
        std::vector<ProseCallback> found;
        for (const std::string& line : lines) {
            llvm::StringRef rest = line;
            llvm::SmallVector<llvm::StringRef, 5> groups;
            while (prose.match(rest, &groups)) {
                found.push_back(proseCallback(groups));
                rest = rest.substr(static_cast<size_t>(groups[0].end() - rest.begin()));
            }
        }
        return found;
    }

    CallbackSig eventProse(const std::vector<std::string>& lines) {
        CallbackSig sig;
        sig.origin = CallbackOrigin::DocComment;
        for (const std::string& raw : lines) {
            llvm::StringRef line = llvm::StringRef(raw).trim();
            if (line.consume_front("Params:")) {
                sig.prose = line.trim().str();
                for (const std::string& part : splitTopLevel(line)) {
                    sig.params.push_back(paramFromText(part));
                }
            }
            else if (line.consume_front("Return:")) {
                sig.returns.push_back(textType(line));
            }
        }
        return sig;
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
