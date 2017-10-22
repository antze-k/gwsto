// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#include "filter.h"

std::string filter::include(const std::wstring& pattern)
{
    try { m_patterns.emplace_back(pattern, true); }
    catch (const std::regex_error& e) { return e.what(); }
    return std::string();
}

std::string filter::exclude(const std::wstring& pattern)
{
    try { m_patterns.emplace_back(pattern, false); }
    catch (const std::regex_error& e) { return e.what(); }
    return std::string();
}

bool filter::includes(const std::wstring& file) const
{
    if (m_patterns.empty())
        return true;

    bool included = !m_patterns.front().include;
    for (const auto& pattern : m_patterns)
    {
        if (std::regex_match(file, pattern.r))
            included = pattern.include;
    }

    return included;
}
