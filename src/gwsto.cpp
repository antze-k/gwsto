// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#include <algorithm>
#include <string>
#include <vector>

#include <locale>
#include <codecvt>
#include <regex>
#include <utility>

#include <shlobj.h>

#pragma comment(lib, "shell32.lib")

#include "file.h"
#include "filter.h"
#include "string_tools.h"

#define _file_exists(name) (0==_waccess((name),0))

enum message_t
{
    message_info,
    message_warning,
    message_error,
};

inline void message(message_t type, const std::string& msg)
{
    switch (type)
    {
        case message_info: fprintf(stdout, "%s\n", msg.c_str()); break;
        case message_warning: fprintf(stderr, "%s\n", msg.c_str()); break;
        case message_error: fprintf(stderr, "%s\n", msg.c_str()); break;
    }
}

static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> utf_converter;

enum command_action
{
    action_pack,
    action_unpack,
    action_remove,
    action_none,
};

struct command
{
    std::wstring path;
    int depth = 0;
    command_action action;
    std::string data;

    inline command() throw() {}
    inline command(const std::wstring& path, int depth, command_action action)
        : path(path), depth(depth), action(action) {}

    static inline bool execution_order(command const* const& a, command const* const& b)
    {
        if (int(a->action) < int(b->action)) return true;
        if (int(a->action) > int(b->action)) return false;
        if (a->depth < b->depth) return true;
        if (a->depth > b->depth) return false;
        return a->path < b->path;
    }

    static inline bool output_order(command const* const& a, command const* const& b)
    {
        if (a->depth < b->depth) return true;
        if (a->depth > b->depth) return false;
        return a->path < b->path;
    }
};

class command_execution_context_interface
{
public:
    virtual void do_pack(command&) = 0;
    virtual void do_unpack(command&) = 0;
    virtual void do_remove(command&) = 0;
    virtual void do_ignore(command&) = 0;
    virtual void do_save_db(const std::vector<command*>&) = 0;
};

class command_list
{
public:
    inline command_list() {}
    inline ~command_list() { for (auto ptr : m_commands) delete ptr; }
    inline void add(const std::wstring& path, int depth, bool included) { m_commands.push_back(new command(path, depth, included ? action_pack : action_remove)); }
    command* merge(const std::wstring& path, int depth, bool included)
    {
        for (auto ptr : m_commands) if (ptr->path == path) return ptr;
        m_commands.push_back(new command(path, depth, included ? action_unpack : action_none));
        return m_commands.back();
    }

    void run(command_execution_context_interface& context)
    {
        std::sort(m_commands.begin(), m_commands.end(), command::execution_order);
        for (auto ptr : m_commands) switch (ptr->action)
        {
            case action_pack: context.do_pack(*ptr); break;
            case action_unpack: context.do_unpack(*ptr); break;
            case action_remove: context.do_pack(*ptr); break;
            case action_none: context.do_ignore(*ptr); break;
        }

        std::sort(m_commands.begin(), m_commands.end(), command::output_order);
        context.do_save_db(m_commands);

        for (auto ptr : m_commands)
            if (ptr->action == action_remove)
                context.do_remove(*ptr);
    }

private:
    std::vector<command*> m_commands;
};

struct stats
{
    int packed = 0;
    int repacked = 0;
    int unpacked = 0;
    int ignored = 0;
    int left_out = 0;
    int pack_read_errors = 0;
    int unpack_write_errors = 0;
};

class application : public command_execution_context_interface
{
public:
    application(int argc, const char** argv, const char** env)
        : m_argc(argc)
        , m_argv(argv)
        , m_env(env)
    {
        setup_paths();

            // TODO: improved argument handling
            // grabs first argument as tag
        if (m_argc >= 2)
            m_tag = m_argv[1];
    }

    int run()
    {
        if (m_document_root.empty())
            return 1;

        load_ini();
        enumerate_dir();
        merge_csv();
        m_command_list.run(*this);
        clean_dir();

        std::string report;
        report.append("========== gwsto: ");
        report.append(std::to_string(m_stats.packed));
        report.append(" template(s) packed, ");
        report.append(std::to_string(m_stats.repacked));
        report.append(" repacked, ");
        report.append(std::to_string(m_stats.unpacked));
        report.append(" unpacked, ");
        report.append(std::to_string(m_stats.left_out));
        report.append(" left out ==========");
        message(message_info, report);

        return 0;
    }

private:
    void load_ini()
    {
        std::wstring ini_path;
        if (_file_exists((m_application_root + L"gwsto.ini").c_str()))
            ini_path = m_application_root + L"gwsto.ini";
        else
        {
            wchar_t path[MAX_PATH];
            if (S_OK == SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))
                if (_file_exists((fix(std::wstring(path)) + L"gwsto/gwsto.ini").c_str()))
                    ini_path = fix(std::wstring(path)) + L"gwsto/gwsto.ini";
        }

        if (ini_path.empty())
        {
            message(message_warning, "no configuration file (gwsto.ini) found\n");
            return;
        }

        file ini;
        if (!ini.read(ini_path))
        {
            message(message_error, "error reading configuration file (gwsto.ini)\n");
            return;
        }

            // remove \r
        ini.data.erase(std::remove_if(ini.data.begin(), ini.data.end(), [](char c) { return c == '\r'; }), ini.data.end());

            // parse lines

        std::string section;
        string_split(ini.data.c_str(), '\n', [&](int index, const std::string& line)
        {
            std::smatch m;

            std::regex regex_section("^\\s*\\[(\\w+)\\]\\s*$", std::regex_constants::ECMAScript);
            if (std::regex_match(line, m, regex_section) && m.size() >= 2) { section = m[1].str(); return; }

            if (section.empty())
                return;

            std::regex regex_keyvalue("^\\s*(\\w+)\\s*\\=\\s*(.*)", std::regex_constants::ECMAScript);
            if (std::regex_match(line, m, regex_keyvalue) && m.size() >= 3)
                load_ini_set_keyvalue(section, m[1].str(), m[2].str());
        });
    }

    void load_ini_set_keyvalue(const std::string& section, const std::string& key, const std::string& value)
    {
        if (strcaseequals(section, "root") && strcaseequals(key, "format"))
            m_zip = strcaseequals(value, "zip");

        if (m_tag != section)
            return;
        if (strcaseequals(key, "include"))
            m_filter.include(utf_converter.from_bytes(value));
        else if (strcaseequals(key, "exclude"))
            m_filter.exclude(utf_converter.from_bytes(value));
    }

    bool setup_paths()
    {
        wchar_t path[MAX_PATH];
        if (S_OK != SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, path))
            return false;
        m_document_root = fix(std::wstring(path) + L"\\Guild Wars\\Templates\\Skills\\");

        if (GetModuleFileNameW(0, path, MAX_PATH) <= 0 || GetLastError() != ERROR_SUCCESS)
            return false;
        wchar_t* last_slash = wcsrchr(path, L'\\');
        if (last_slash)
            last_slash[1] = L'\0';
        m_application_root = fix(std::wstring(path));

        return true;
    }

    std::wstring fix(const std::wstring& name)
    {
        std::wstring out;
        wchar_t prev = 0;
        for (size_t i=0; i < name.size(); ++i)
        {
            if (is_slash(name[i]))
            {
                if (!is_slash(prev))
                    out.push_back((prev = '/'));
                continue;
            }
            out.push_back((prev = name[i]));
        }

        if (!out.empty() && out.back() != '/')
            out.push_back('/');

        return out;
    }

    inline bool is_slash(wchar_t c)
    {
        return c == '\\' || c == '/';
    }

    void enumerate_dir(const std::wstring& subpath = L"", int depth = 0)
    {
        WIN32_FIND_DATA file;
        HANDLE found = FindFirstFileExW((m_document_root + subpath + L'*').c_str(),
            FindExInfoStandard, &file, FindExSearchNameMatch, 0, 0);
        if (found == INVALID_HANDLE_VALUE)
            return;
    
        do
        {
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                const std::wstring dir_name(file.cFileName);
                if (dir_name == L"." || dir_name == L"..")
                    continue;
    
                enumerate_dir(subpath + dir_name + L"/", depth + 1);
                continue;
            }
    
            std::wstring filename(file.cFileName);
            if (filename.size() < 4 || filename.substr(filename.size() - 4, 4) != L".txt")
                continue;
    
            std::wstring full_filename(subpath + file.cFileName);
            m_command_list.add(full_filename, depth, m_filter.includes(full_filename));
        } while (FindNextFileW(found, &file));
    
        FindClose(found);
    }

    void merge_csv()
    {
        file csv;
        if (!csv.read(m_document_root + L"templates.csv"))
            return;

        size_t cursor = 0;
        size_t delim;
        do
        {
            delim = csv.data.find('\n', cursor);
            delim = (delim == std::string::npos) ? csv.data.size() : delim;
            merge_command(&csv.data[cursor], delim - cursor);
            cursor = delim + 1;
        } while (cursor < csv.data.size());
    }

    bool clean_dir(const std::wstring& subpath = L"", int depth = 0)
    {
        WIN32_FIND_DATA file;
        HANDLE found = FindFirstFileExW((m_document_root + subpath + L'*').c_str(),
            FindExInfoStandard, &file, FindExSearchNameMatch, 0, 0);
        if (found == INVALID_HANDLE_VALUE)
            return false;
    
        size_t files_count = 0;
        do
        {
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                const std::wstring dir_name(file.cFileName);
                if (dir_name == L"." || dir_name == L"..")
                    continue;
    
                const bool cleaned = clean_dir(subpath + dir_name + L"/", depth + 1);
                if (!cleaned)
                    ++files_count;
                continue;
            }
    
            ++files_count;
        } while (FindNextFileW(found, &file));
    
        FindClose(found);

        if (files_count == 0 && !subpath.empty())
        {
            _wrmdir((m_document_root + subpath).c_str());
            return true;
        }
        return false;
    }

    void create_dir_for(const std::wstring& root, const std::wstring& file)
    {
        size_t pos = 0;
        while ((pos = file.find(L'/', pos)) != std::wstring::npos)
        {
            _wmkdir((root + file.substr(0, pos)).c_str());
            ++pos;
        }
    }

private:
    void merge_command(const char* line, size_t length)
    {
        std::string cline(line, length);
        if (cline.empty())
            return;
        bool empty = true;
        for (char c : cline) if (!::isspace(c)) { empty = false; break; }
        if (empty)
            return;

        std::wstring wline = utf_converter.from_bytes(cline);
        size_t colon = wline.find(L',');
        if (colon == std::wstring::npos)
            return;

        std::wstring full_filename = wline.substr(0, colon);
        int depth = 0;
        for (wchar_t c : full_filename) if (c == L'/') ++depth;
        m_command_list.merge(full_filename, depth, m_filter.includes(full_filename))->data =
            utf_converter.to_bytes(wline.substr(colon + 1));
    }

private:
    virtual void do_pack(command& cmd) override
    {
        if (cmd.data.empty())
        {
            ++m_stats.packed;
            message(message_info, std::string("adding ") + utf_converter.to_bytes(cmd.path) + "...");
        }

        file f;
        if (!f.read(m_document_root + cmd.path))
        {
            ++m_stats.pack_read_errors;
            return;
        }

        f.data.erase(std::remove_if(f.data.begin(), f.data.end(), [](char c) {return c == '\r' || c == '\n'; }), f.data.end());

        if (!cmd.data.empty() && cmd.data != f.data)
        {
            ++m_stats.repacked;
            message(message_info, std::string("updating ") + utf_converter.to_bytes(cmd.path) + "...");
        }

        cmd.data = std::move(f.data);

        if (cmd.data.empty())
            ++m_stats.pack_read_errors;
    }

    virtual void do_unpack(command& cmd) override
    {
        ++m_stats.unpacked;
        message(message_info, std::string("unpacking ") + utf_converter.to_bytes(cmd.path) + "...");

        create_dir_for(m_document_root, cmd.path);

        file f;
        f.data = std::move(cmd.data);
        if (!f.write(m_document_root + cmd.path))
        {
            cmd.data = std::move(f.data);
            ++m_stats.unpack_write_errors;
            return;
        }
        cmd.data = std::move(f.data);
    }

    virtual void do_remove(command& cmd) override
    {
        ++m_stats.left_out;
        _wremove((m_document_root + cmd.path).c_str());
    }

    virtual void do_ignore(command&) override
    {
        ++m_stats.ignored;
    }

    virtual void do_save_db(const std::vector<command*>& commands) override
    {
        FILE* fp = nullptr;
        _wfopen_s(&fp, (m_document_root + L"templates.csv").c_str(), L"wb");
        if (!fp)
        {
            message(message_error, "error saving db");
            return;
        }

        for (auto ptr : commands)
        {
            std::string line = utf_converter.to_bytes(ptr->path);
            line.push_back(',');
            line.append(ptr->data);
            line.push_back('\n');

            fwrite(line.c_str(), sizeof(char), line.size(), fp);
        }

        fclose(fp);
    }

private:
    int m_argc;
    const char** m_argv;
    const char** m_env;

    bool m_zip = false;
    std::string m_tag;

    std::wstring m_document_root;
    std::wstring m_application_root;
    filter m_filter;
    command_list m_command_list;
    stats m_stats;
};

int main(int argc, const char* argv[], const char* env[])
{
    application app(argc, argv, env);
    return app.run();
}

