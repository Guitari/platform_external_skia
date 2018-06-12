/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "NFAtoDFA.h"
#include "RegexParser.h"

#include <fstream>
#include <sstream>
#include <string>

/**
 * Processes a .lex file and produces .h and .cpp files which implement a lexical analyzer. The .lex
 * file is a text file with one token definition per line. Each line is of the form:
 * <TOKEN_NAME> = <pattern>
 * where <pattern> is either a regular expression (e.g [0-9]) or a double-quoted literal string.
 */

static constexpr const char* HEADER =
    "/*\n"
    " * Copyright 2017 Google Inc.\n"
    " *\n"
    " * Use of this source code is governed by a BSD-style license that can be\n"
    " * found in the LICENSE file.\n"
    " */\n"
    "/*****************************************************************************************\n"
    " ******************** This file was generated by sksllex. Do not edit. *******************\n"
    " *****************************************************************************************/\n";

void writeH(const DFA& dfa, const char* lexer, const char* token,
            const std::vector<std::string>& tokens, const char* hPath) {
    std::ofstream out(hPath);
    SkASSERT(out.good());
    out << HEADER;
    out << "#ifndef SKSL_" << lexer << "\n";
    out << "#define SKSL_" << lexer << "\n";
    out << "#include <cstddef>\n";
    out << "#include <cstdint>\n";
    out << "namespace SkSL {\n";
    out << "\n";
    out << "struct " << token << " {\n";
    out << "    enum Kind {\n";
    for (const std::string& t : tokens) {
        out << "        #undef " << t << "\n";
        out << "        " << t << ",\n";
    }
    out << "    };\n";
    out << "\n";
    out << "    " << token << "()\n";
    out << "    : fKind(Kind::INVALID)\n";
    out << "    , fOffset(-1)\n";
    out << "    , fLength(-1) {}\n";
    out << "\n";
    out << "    " << token << "(Kind kind, int offset, int length)\n";
    out << "    : fKind(kind)\n";
    out << "    , fOffset(offset)\n";
    out << "    , fLength(length) {}\n";
    out << "\n";
    out << "    Kind fKind;\n";
    out << "    int fOffset;\n";
    out << "    int fLength;\n";
    out << "};\n";
    out << "\n";
    out << "class " << lexer << " {\n";
    out << "public:\n";
    out << "    void start(const char* text, size_t length) {\n";
    out << "        fText = text;\n";
    out << "        fLength = length;\n";
    out << "        fOffset = 0;\n";
    out << "    }\n";
    out << "\n";
    out << "    " << token << " next();\n";
    out << "\n";
    out << "private:\n";
    out << "    const char* fText;\n";
    out << "    int fLength;\n";
    out << "    int fOffset;\n";
    out << "};\n";
    out << "\n";
    out << "} // namespace\n";
    out << "#endif\n";
}

void writeCPP(const DFA& dfa, const char* lexer, const char* token, const char* include,
              const char* cppPath) {
    std::ofstream out(cppPath);
    SkASSERT(out.good());
    out << HEADER;
    out << "#include \"" << include << "\"\n";
    out << "\n";
    out << "namespace SkSL {\n";
    out << "\n";

    size_t states = 0;
    for (const auto& row : dfa.fTransitions) {
        states = std::max(states, row.size());
    }
    out << "static int16_t mappings[" << dfa.fCharMappings.size() << "] = {\n    ";
    const char* separator = "";
    for (int m : dfa.fCharMappings) {
        out << separator << std::to_string(m);
        separator = ", ";
    }
    out << "\n};\n";
    out << "static int16_t transitions[" << dfa.fTransitions.size() << "][" << states << "] = {\n";
    for (size_t c = 0; c < dfa.fTransitions.size(); ++c) {
        out << "    {";
        for (size_t j = 0; j < states; ++j) {
            if ((size_t) c < dfa.fTransitions.size() && j < dfa.fTransitions[c].size()) {
                out << " " << dfa.fTransitions[c][j] << ",";
            } else {
                out << " 0,";
            }
        }
        out << " },\n";
    }
    out << "};\n";
    out << "\n";

    out << "static int8_t accepts[" << states << "] = {";
    for (size_t i = 0; i < states; ++i) {
        if (i < dfa.fAccepts.size()) {
            out << " " << dfa.fAccepts[i] << ",";
        } else {
            out << " " << INVALID << ",";
        }
    }
    out << " };\n";
    out << "\n";

    out << token << " " << lexer << "::next() {\n";;
    out << "    int startOffset = fOffset;\n";
    out << "    if (startOffset == fLength) {\n";
    out << "        return " << token << "(" << token << "::END_OF_FILE, startOffset, 0);\n";
    out << "    }\n";
    out << "    int offset = startOffset;\n";
    out << "    int state = 1;\n";
    out << "    " << token << "::Kind lastAccept = " << token << "::Kind::INVALID;\n";
    out << "    int lastAcceptEnd = startOffset + 1;\n";
    out << "    while (offset < fLength) {\n";
    out << "        if ((uint8_t) fText[offset] >= " << dfa.fCharMappings.size() << ") {";
    out << "            break;";
    out << "        }";
    out << "        state = transitions[mappings[(int) fText[offset]]][state];\n";
    out << "        ++offset;\n";
    out << "        if (!state) {\n";
    out << "            break;\n";
    out << "        }\n";
    out << "        if (accepts[state]) {\n";
    out << "            lastAccept = (" << token << "::Kind) accepts[state];\n";
    out << "            lastAcceptEnd = offset;\n";
    out << "        }\n";
    out << "    }\n";
    out << "    fOffset = lastAcceptEnd;\n";
    out << "    return " << token << "(lastAccept, startOffset, lastAcceptEnd - startOffset);\n";
    out << "}\n";
    out << "\n";
    out << "} // namespace\n";
}

void process(const char* inPath, const char* lexer, const char* token, const char* hPath,
             const char* cppPath) {
    NFA nfa;
    std::vector<std::string> tokens;
    tokens.push_back("END_OF_FILE");
    std::string line;
    std::ifstream in(inPath);
    while (std::getline(in, line)) {
        std::istringstream split(line);
        std::string name, delimiter, pattern;
        if (split >> name >> delimiter >> pattern) {
            SkASSERT(split.eof());
            SkASSERT(name != "");
            SkASSERT(delimiter == "=");
            SkASSERT(pattern != "");
            tokens.push_back(name);
            if (pattern[0] == '"') {
                SkASSERT(pattern.size() > 2 && pattern[pattern.size() - 1] == '"');
                RegexNode node = RegexNode(RegexNode::kChar_Kind, pattern[1]);
                for (size_t i = 2; i < pattern.size() - 1; ++i) {
                    node = RegexNode(RegexNode::kConcat_Kind, node,
                                     RegexNode(RegexNode::kChar_Kind, pattern[i]));
                }
                nfa.addRegex(node);
            }
            else {
                nfa.addRegex(RegexParser().parse(pattern));
            }
        }
    }
    NFAtoDFA converter(&nfa);
    DFA dfa = converter.convert();
    writeH(dfa, lexer, token, tokens, hPath);
    writeCPP(dfa, lexer, token, (std::string("SkSL") + lexer + ".h").c_str(), cppPath);
}

int main(int argc, const char** argv) {
    if (argc != 6) {
        printf("usage: sksllex <input.lex> <lexername> <tokenname> <output.h> <output.cpp>\n");
        exit(1);
    }
    process(argv[1], argv[2], argv[3], argv[4], argv[5]);
    return 0;
}
