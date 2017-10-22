// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#include "file.h"

#include <cstdio>

bool file::read(const std::wstring& path, size_t size_limit)
{
    FILE* fp = nullptr;
    _wfopen_s(&fp, path.c_str(), L"rb");
    if (!fp)
        return false;

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    if (size_limit && size > size_limit) { fclose(fp); return false; }
    fseek(fp, 0, SEEK_SET);
    try { data.resize(size); }
    catch (...) { fclose(fp); return false; }
    if (size != fread(&data[0], sizeof(char), size, fp))
        data.clear();
    fclose(fp);
    return true;
}

bool file::write(const std::wstring& path)
{
    FILE* fp = nullptr;
    _wfopen_s(&fp, path.c_str(), L"wb");
    if (!fp)
        return false;

    if (data.size() != fwrite(&data[0], sizeof(char), data.size(), fp))
    {
        fclose(fp);
        return false;
    }

    fclose(fp);
    return true;
}
