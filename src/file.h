// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#pragma once

#include <string>

struct file
{
    std::string data;

    bool read(const std::wstring& path, size_t size_limit = 0);
    bool write(const std::wstring& path);
};
