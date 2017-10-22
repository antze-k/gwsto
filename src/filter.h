// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#pragma once

#include <string>
#include <regex>

class filter
{
public:
    std::string include(const std::wstring& pattern);  // returns regex errors
    std::string exclude(const std::wstring& pattern);  // returns regex errors

    bool includes(const std::wstring& file) const;

private:
    struct pattern_t
    {
        std::wregex r;
        bool include;
        pattern_t(const std::wstring& p, bool include) : r(p, std::regex_constants::ECMAScript), include(include) {}
    };

    std::vector<pattern_t> m_patterns;
};
