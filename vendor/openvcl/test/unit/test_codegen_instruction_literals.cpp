#include "test_harness.h"

#include "../../src/VuInstructionInfo.h"

#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string readTextFile(const std::string& path)
    {
        std::ifstream input(path.c_str());
        std::ostringstream text;
        text << input.rdbuf();
        return text.str();
    }

    std::vector<std::string> extractStringLiterals(const std::string& text)
    {
        std::vector<std::string> literals;
        for (std::string::size_type i = 0; i < text.size(); ++i)
        {
            if (text[i] != '"')
                continue;

            std::string literal;
            ++i;
            for (; i < text.size(); ++i)
            {
                if (text[i] == '\\')
                {
                    if (i + 1 < text.size())
                    {
                        literal += text[i];
                        literal += text[i + 1];
                        ++i;
                    }
                    continue;
                }
                if (text[i] == '"')
                    break;
                literal += text[i];
            }
            literals.push_back(literal);
        }
        return literals;
    }

    std::string lower(const std::string& value)
    {
        std::string result = value;
        for (std::string::size_type i = 0; i < result.size(); ++i)
            result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
        return result;
    }

    bool isMnemonicBoundary(char c)
    {
        return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
    }

    bool containsMnemonicWord(const std::string& text, const std::string& mnemonic)
    {
        std::string lowered = lower(text);
        std::string::size_type pos = lowered.find(mnemonic);
        while (pos != std::string::npos)
        {
            const bool left = pos == 0 || isMnemonicBoundary(lowered[pos - 1]);
            const std::string::size_type end = pos + mnemonic.size();
            const bool right = end >= lowered.size() || isMnemonicBoundary(lowered[end]);
            if (left && right)
                return true;
            pos = lowered.find(mnemonic, pos + 1);
        }
        return false;
    }

    std::set<std::string> instructionWords()
    {
        std::set<std::string> words;
        for (const vcl::VuInstructionInfo* info = vcl::allVuInstructionInfos(); info->mnemonic; ++info)
            words.insert(info->mnemonic);

        const char* broadcastForms[] = {
            "addax", "adday", "addy", "addw",
            "maddaw", "maddax", "madday", "maddaz", "maddw", "maddx", "maddy", "maddz",
            "maxx", "miniw",
            "mulax", "mulay", "mulaz", "mulw", "mulz",
            0
        };
        for (const char** word = broadcastForms; *word; ++word)
            words.insert(*word);
        return words;
    }
}

TEST_CASE("CodeGenerator: VU op mnemonics are emitted through metadata helpers")
{
    const std::string path = std::string(OPENVCL_SOURCE_ROOT) + "/src/CodeGenerator.cpp";
    const std::string source = readTextFile(path);
    REQUIRE(!source.empty());

    const std::vector<std::string> literals = extractStringLiterals(source);
    const std::set<std::string> words = instructionWords();

    for (std::vector<std::string>::const_iterator literal = literals.begin(); literal != literals.end(); ++literal)
    {
        for (std::set<std::string>::const_iterator word = words.begin(); word != words.end(); ++word)
        {
            if (containsMnemonicWord(*literal, *word))
            {
                fprintf(stderr, "  hardcoded VU mnemonic '%s' in CodeGenerator string literal: \"%s\"\n",
                        word->c_str(), literal->c_str());
                CHECK(false);
            }
        }
    }
}
