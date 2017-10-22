// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#pragma once

#include <functional>
#include <cstring>
#include <string>

void string_split(const char* str, char separator, std::function<void(int, const std::string&)> receiver)
{
    if (!str)
        return;

    const char* ptr;
    int index = 0;
    for (;;)
    {
        ptr = strchr(str, separator);
        if (!ptr)
        {
            if (index > 0 || str[0])
                receiver(index, std::string(str));
            break;
        }
        else
        {
            receiver(index, std::string(str, ptr - str));
            str = ptr + 1;
        }
        index++;
    }
}

std::string string_combine(char separator, std::function<bool(int, std::string&)> provider)
{
    int index = 0;
    std::string next;
    std::string output;
    while (provider(index, next))
    {
        if (index > 0)
            output.push_back(separator);
        output.append(next);
        ++index;
    }
    return output;
}

bool strcaseequals(const std::string& s1, const std::string& s2)
{
    return 0 == _stricmp(s1.c_str(), s2.c_str());
}