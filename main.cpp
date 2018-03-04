
// Copyright (c) 2018, Aaron Michaux
// All rights reserved.

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
                    
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <libgen.h>
#include <ftw.h>

#include <vector>
#include <string>
#include <string_view>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <cctype>
#include <regex>
#include <deque>

using std::vector;
using std::string;
using std::string_view;
using std::istream;
using std::ostream;
using std::unordered_map;
using std::cout;
using std::endl;

// ------------------------------------------------------------ global variables

static vector<string> nftw_files;

// ------------------------------------------------------------------ structures

struct Options
{
    bool show_help{false};
    bool has_error{false};
    
    string in_file{""};
    string out_file{""};

    std::unordered_map<string, string> defines;
};

struct State
{
    State(const Options& opts_, istream& in_, ostream& out_)
        : opts(opts_), in(in_), out(out_)
    {
        auto cwd = getcwd(nullptr, 0);
        current_working_directory = cwd;
        free(cwd);

        // for stdin, stdout, stderr
        n_descriptors = sysconf(_SC_OPEN_MAX) - 3;

        env.insert(opts.defines.begin(), opts.defines.end());
    }

    bool has_error{false};

    const Options& opts;
    istream& in;
    ostream& out;
    string current_working_directory{""};
    vector<string> additional_filenames;
    unordered_map<string, string> env; // cached environment variables

    int n_descriptors{0};
};

struct FileCommand
{
    string pattern;
    std::regex glob;
    string command;
    vector<string> re_add_outputs;
    vector<string> other_outputs;
};

struct FilterVariable
{
    string variable;
    string filter;
    std::regex glob;
    std::deque<string> products;
};

// -------------------------------------------------------------- predefinitions

static void show_help(const char * exec_name);
static Options parse_commandline(int argc, char * * argv);

// Environment variables
static const string& getenv(State& state, const string& s);
static void substitute_env_variables(State& state, string& line_s);

// String functions
static bool starts_with(const std::string& s, const std::string& prefix);
static void ltrim(std::string& s);
static void rtrim(std::string& s);
static void trim(std::string& s);
static bool is_empty_line(const std::string& s);

// Traverses directory, placing found filenames into a global
// variable (nftw_files)
static int nftw_fn(const char * fpath, const struct stat * sb,
                   int typeflag, struct FTW * ftwbuf);

// Turns a glob pattern into a regex.
static std::regex make_glob(const string& pattern);

// Runs an individual matched substitution
static void command_substitute(State& state,
                               const string& fname,
                               const FileCommand& cmd,
                               vector<FilterVariable>& filters);

// Runs an entire +src command
static void process_src_command(State& state, const vector<string> command);

// The actual pipeline
static bool transform_input(State& state);

// ------------------------------------------------------------------- show-help

static void show_help(const char * exec_name)
{
    printf(R"V0G0N(
   Usage: %s [OPTIONS...] -i <filename>

      -i <filename>    Input filename (required); '-' for stdin
      -o <filename>    Output filename
      -D <var=value>   Set variable 'var' to 'value', as if an 
                       environment variable.=

   Mobuis is a preprocessor for ninja.build files. It adds two features:
   (1) Environment variable substitution using ${USER} like syntax.
   (2) +src commands that search directory structures and generate build rules.

   An example +src command is as follows:

      +src cd=.. OBJS=*.o src tests
      - */messages/*.cpp  build $builddir/%%.o:   cpp ^ | $builddir/%%.pb.h
        ~ cpp_flags = $cpp_flags -I$builddir/@
      - *.cpp         build $builddir/%%.o:   cpp_pch ^ | $builddir/$pchfile.gch
      - *.proto       build $builddir/%%.pb.cc! $builddir/%%.pb.h: proto ^

   '+src' starts the command. All subsequent lines being with '-' or '~'.
   The command cds up one directory, and searches the 'src' and 'tests'
   directories -- geting a listing of all files. Any outputs that match '*.o'
   are put into the OBJS environment variable, which can be later expanded.

   Every file is matched against the first possible '-' line.
   (A '~' line is merely an extension of the previous '-' line.)
   When the match is made, then output is generated by text substitution, where:
   + '%%' matches the filename without extension
   + '^' is the full filename
   + '@' is dirname(filename)
   + '&' is basename(filename)
   If a ninja build product ends in a '!', then it is added to the list
   of files found on the +src line, to be later processed through 
   the '-' sequence.

)V0G0N",
           exec_name);
}

// ---------------------------------------------------------- parse command-line

static Options parse_commandline(int argc, char * * argv)
{
    Options opts;

    auto safe_s = [&] (int& ind) -> string {
        ++ind;
        if(ind >= argc) {
            fprintf(stderr, "Expected argument after '%s'.\n", argv[ind]);
            opts.has_error = true;
            return "";
        }
        return argv[ind];
    };

    auto process_define = [&] (const string& s) {
        auto pos = s.find('=');
        if(pos == string::npos)
            opts.defines[s] = "";
        else
            opts.defines[s.substr(0, pos)] = s.substr(pos+1);
    };
    
    for(int i = 1; i < argc && !opts.has_error; ++i) {
        string arg = argv[i];
        if(arg == "-h" || arg == "--help") { opts.show_help = true; continue; }
        if(arg == "-i") { opts.in_file = safe_s(i); continue; }
        if(arg == "-")  { opts.in_file = arg; continue; }
        if(arg == "-o") { opts.out_file = safe_s(i); continue; }
        if(arg == "-D") { process_define(safe_s(i)); continue; }
        if(starts_with(arg, "-D")) { process_define(&arg[2]); continue; }
    }

    if(!opts.show_help && opts.in_file == "") {
        fprintf(stderr, "Must specify an input file.\n");
        opts.has_error = true;
    }

    return opts;
}

// ------------------------------------------------------------------------ main

int main(int argc, char * * argv)
{    
    // -- Parse command-line
    auto opts = parse_commandline(argc, argv);

    // -- Analyze options
    if(opts.show_help) {
        show_help(basename(argv[0]));
        return EXIT_SUCCESS;
    } else if(opts.has_error) {        
        return EXIT_FAILURE;
    }

    // -- Setup input/output files
    std::istream * in{nullptr};
    std::ostream * out{nullptr};
    std::ifstream fin;
    std::ofstream fout;
    if(opts.in_file == "-") {
        in = &std::cin;
    } else {
        try {
            fin.open(opts.in_file);
            in = &fin;
        } catch(...) {
        }
    }

    if(opts.out_file == "-" || opts.out_file == "")
        out = &std::cout;
    else {
        try {
            fout.open(opts.out_file);
            out = &fout;
        } catch(...) {
        }
    }
    
    if(!in->good())
        fprintf(stderr,
                "failed to open input file '%s'\n",
                opts.in_file.c_str());
    if(!out->good())
        fprintf(stderr,
                "failed to open output file '%s'\n",
                opts.out_file.c_str());

    // -- Transform input into output
    try {
        if(in->good() && out->good()) {
            State state(opts, *in, *out);
            if(transform_input(state))
                return EXIT_SUCCESS;
        }            
    } catch(std::exception& e) {
        string err = e.what();
        fprintf(stderr, "exception tranforming input: \"%s\"\n", err.c_str());
    }
    
    // -- Done
    return EXIT_FAILURE;
}

// ------------------------------------------------ cached environment variables

static const string& getenv(State& state, const string& s)
{
    auto ii = state.env.find(s);
    if(ii == state.env.end()) {
        auto var = getenv(s.c_str());
        if(var == nullptr)
            throw std::runtime_error("environment variable '"
                                     + s + "' not found");
        state.env[s] = string(var);
        ii = state.env.find(s);
    }
    return ii->second;
}

static void substitute_env_variables(State& state, string& line_s)
{
    auto pos = line_s.find('$');
    if(pos == string::npos) return;

    string_view line(line_s);
    const auto len = int(line.size());
    
    std::stringstream ss("");
    ss << line.substr(0, pos);

    auto process_variable = [&] (int& i) {
        assert(line[i] == '$');
        if(++i == len)
            return;
        if(line[i] != '{') {
            ss << '$';
            ss << line[i];
            return;
        }

        auto pos = line.find('}', i);
        if(pos == string::npos)
            throw std::runtime_error("parse error reading variable name, "
                                     "missing '}'");
        string variable(line.substr(i + 1, pos - i - 1));
        ss << getenv(state, variable);
        i = pos;
    };
    
    for(auto i = int(pos); i < len; ++i) {
        if(line[i] == '$')
            process_variable(i);
        else 
            ss << line[i];
    }
    
    line_s = ss.str();    
}

// -----------------------------------------------------------------------------
// --                           String Funcitons                              --
// -----------------------------------------------------------------------------

// ----------------------------------------------------------------- starts-with

static bool starts_with(const std::string& s, const std::string& prefix)
{
    auto pos = 0u;
    while(pos < s.size() && std::isspace(s[pos]))
        ++pos;
    return std::mismatch(prefix.begin(), prefix.end(),
                         s.begin() + pos, s.end()).first == prefix.end();
}

// ------------------------------------------------------------------------ trim

static void ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static void rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static void trim(std::string& s)
{
    ltrim(s);
    rtrim(s);
}

static bool is_empty_line(const std::string& s)
{
    for(auto c: s)
        if(!std::isspace(c))
            return false;
    return true;
}

// http://man7.org/linux/man-pages/man3/nftw.3.html
static int nftw_fn(const char * fpath, const struct stat * sb,
                   int typeflag, struct FTW * ftwbuf)
{
    if(typeflag == FTW_F)
        nftw_files.push_back(fpath);
    return 0; // continue traversal
}

// ------------------------------------------------------------------- make-glob

static std::regex make_glob(const string& pattern)
{
    int len = pattern.size();
    int pos = 0;
    char * buffer = static_cast<char *>(alloca(len * 2 + 1));
    const char * src = pattern.c_str();

    while(*src) {
        switch(*src) {
        case '\\': buffer[pos++] = '\\'; buffer[pos++] = '\\'; break;
        case '^':  buffer[pos++] = '\\'; buffer[pos++] = '^'; break;
        case '.':  buffer[pos++] = '\\'; buffer[pos++] = '.'; break;
        case '$':  buffer[pos++] = '\\'; buffer[pos++] = '$'; break;
        case '|':  buffer[pos++] = '\\'; buffer[pos++] = '|'; break;
        case '(':  buffer[pos++] = '\\'; buffer[pos++] = '('; break;
        case ')':  buffer[pos++] = '\\'; buffer[pos++] = ')'; break;
        case '[':  buffer[pos++] = '\\'; buffer[pos++] = '['; break;
        case ']':  buffer[pos++] = '\\'; buffer[pos++] = ']'; break;
        case '+':  buffer[pos++] = '\\'; buffer[pos++] = '+'; break;
        case '/':  buffer[pos++] = '\\'; buffer[pos++] = '/'; break;
        case '?':  buffer[pos++] = '.';  break;
        case '*':  buffer[pos++] = '.';  buffer[pos++] = '*'; break;
        default:   buffer[pos++] = *src;
        }
        src++;
    }
    buffer[pos++] = '\0';

    return std::regex(buffer);
}

// ---------------------------------------------------------- command-substitute

static void command_substitute(State& state,
                               const string& fname,
                               const FileCommand& cmd,
                               vector<FilterVariable>& filters)
{
    // substitute:
    //    '^' for fname
    //    '%' for the filename without extension
    //    '@' for dirname(fname)
    //    '&' for basename(fname)
    // remove all '!', but...
    // if a '!' exists, then add that product to the list of files in state    

    // Remove leading './' if it is there
    const char * fname_ptr = fname.c_str();
    if(fname.size() > 2 && fname_ptr[0] == '.' && fname_ptr[1] == '/')
        fname_ptr += 2;

    // Create the extensionless version
    const char * extless_ptr = fname_ptr;
    {
        int len = strlen(extless_ptr);
        int i = len - 1;
        for(; i >= 0; --i) {
            if(extless_ptr[i] == '.') break;
            if(extless_ptr[i] == '/') break;
        }

        if(extless_ptr[i] == '.') {
            // alloca the extensionless version
            char * ptr = static_cast<char *>(alloca(i));
            memcpy((void *) ptr, (void *) fname_ptr, i);
            ptr[i] = '\0';
            extless_ptr = ptr;
        }
    }
    
    auto get = [&] (const char * s, char * (*f) (char *)) {
        auto len = strlen(s);
        auto s2 = static_cast<char *>(malloc((len + 1) * sizeof(char)));
        strcpy(s2, s);
        auto ret = string(f(s2));
        free(s2);
        return ret;
    };

    // Output the command
    auto process_text = [&] (const string& s, std::ostream& out) {
        for(auto c: s) {
            switch(c) {
            case '^': out << fname_ptr; break;
            case '%': out << extless_ptr; break;
            case '@': out << get(fname_ptr, dirname).c_str(); break;
            case '&': out << get(fname_ptr, basename).c_str(); break;
            case '!': break;
            default:  out << c;
            }
        }
    };

    process_text(cmd.command, state.out);

    auto handle_output = [&] (const string& s) {
        for(auto& filter: filters)
            if(std::regex_match(s.begin(), s.end(), filter.glob))
                filter.products.push_back(s);
    };
    
    // And add in the outputs with '!' on them
    for(const auto& s: cmd.re_add_outputs) {
        std::stringstream ss("");
        process_text(s, ss);
        auto output = ss.str();
        handle_output(output);
        state.additional_filenames.push_back(output);
    }

    for(const auto& s: cmd.other_outputs) {
        std::stringstream ss("");
        process_text(s, ss);
        handle_output(ss.str());
    }

    state.out << endl;
}

// --------------------------------------------------------- process-src-command

static void process_src_command(State& state, const vector<string> command)
{
    // +src VAR=*.o FOO=*.pb.h cd=../src dir1 dir2 dir3
    // - *.proto       $builddir/%.pb.h $builddir/%.pb.cc!: proto   $
    // - *tweaker.cpp  $builddir/%.o:               cpp_O3  $  | $root/zap.pb.h
    //   ~ o_flag = -O1
    // - *.cpp         $builddir/%.o:               cpp     $
    //
    // where:
    // +src indicates the start of a source command
    //  - VAR=*.o and FOO=*.pb.h indicates that enviroment variable VAR
    //    (and FOO) are set to globbed output file matches for all the rules
    //  - cd../src means "change directory before searching"
    //  - dir1 dir2 dir3 are the directories that are searched
    //
    // For each file found, the first matching rule is applied.
    // Rules begin with '-', where:
    //  - *.proto is a wildcard match (can have multiple *)
    //  - % is the matched filename without extension
    //  - ^ is the matched filename
    //  - @ is dirname(filename)
    //  - & is basename(filename)
    //  - !, if an output filename ends !, then it's added to the list of files
    //       found above
    //  - ~, A line beging like this is just a continuation of the previous rule
    
    // ---- Parse the source line...
    string cd_dir = "";
    vector<FilterVariable> filters;
    vector<string> directories;
    {
        vector<string> src_parts;
        std::istringstream iss(command[0]);
        std::copy(std::istream_iterator<string>(iss),
                  std::istream_iterator<string>(),
                  std::back_inserter(src_parts));
        if(src_parts[0] != "+src")
            throw std::runtime_error("failed to match '+src' in command");

        for(auto i = 1u; i < src_parts.size(); ++i) {
            auto& part = src_parts[i];
            auto pos = part.find('=');
            if(pos == string::npos) {
                directories.emplace_back(std::move(part));
            } else {
                string var = part.substr(0, pos);
                string value = part.substr(pos+1);
                if(var == "cd") {
                    if(cd_dir != "")
                        throw std::runtime_error("multiple 'cd' specifiers");
                    cd_dir = std::move(value);
                } else {
                    filters.emplace_back();
                    filters.back().variable = std::move(var);
                    filters.back().glob = make_glob(value);
                    filters.back().filter = std::move(value);
                }
            }
        }
    }
    
    // ---- Parse each of the command lines
    vector<FileCommand> commands;
    for(auto i = 1u; i < command.size(); ++i) {        
        if(starts_with(command[i], "~")) {
            if(commands.size() == 0)
                throw std::runtime_error("misplaced command extension '~'");
            auto pos = command[i].find('~');
            commands.back().command += "\n" + command[i].substr(pos + 1);
            continue;
        }
        
        vector<string> parts;
        std::istringstream iss(command[i]);
        std::copy(std::istream_iterator<string>(iss),
                  std::istream_iterator<string>(),
                  std::back_inserter(parts));
        assert(parts.size() > 0 && parts[0] == "-");
        if(parts.size() < 2)
            throw std::runtime_error("failed to match file rule '"
                                     + command[i] + "'");

        // implode tokens [2..]
        std::stringstream ss("");
        for(auto j = 2u; j < parts.size(); ++j) {
            if(j > 2) ss << " ";
            ss << parts[j];
        }
        
        commands.emplace_back();
        auto& cmd = commands.back();
        cmd.pattern = parts[1];
        cmd.glob = make_glob(cmd.pattern);
        cmd.command = ss.str();

        // Find the outputs (those parts[2..] before the first ':')
        auto last_output_ind = 0u;
        for(auto j = 2u; j < parts.size(); ++j) {
            if(parts[j].back() == ':') {
                last_output_ind = j+1; // 1 past the end
                break;
            }
        }

        // Now add those outputs with shebangs to the 're-add' list
        for(auto j = 2u; j < last_output_ind; ++j) {
            if(parts[j].back() == ':')
                parts[j].pop_back();
            if(parts[j].back() == '!') {
                parts[j].pop_back();
                cmd.re_add_outputs.push_back(parts[j]);
            } else {
                cmd.other_outputs.push_back(parts[j]);
            }
        }
    }

    if(false) {
        cout << "COMMAND " << endl;
        cout << "cd:     " << cd_dir << endl;
        for(auto& f: filters)
            cout << "filter: " << f.variable << " = " << f.filter << endl;
        for(auto& d: directories)
            cout << " + directory '" << d << "'" << endl;
        for(auto& cmd: commands)
            cout << " - command " << cmd.pattern << " => " << cmd.command<<endl;
    }

    // ---- Attempt to change directory
    if(cd_dir != "") {
        if(chdir(cd_dir.c_str()) != 0)
            throw std::runtime_error("failed to change directory to: '"
                                     + cd_dir + "'");
    }

    // ---- Search directories: this is not thread-safe, for what it's worth
    nftw_files.clear();
    for(const auto& directory: directories) {
        if(nftw(directory.c_str(), nftw_fn, state.n_descriptors, 0) != 0)
            throw std::runtime_error("failed to traverse '" + directory + "'");
    }

    // ---- Move back to original CWD if we changed directory
    if(cd_dir != "")
        if(chdir(state.current_working_directory.c_str()) != 0)
            throw std::runtime_error("failed to change directory to: '"
                                     + state.current_working_directory + "'");

    // ---- Match and substitude files against the commands
    auto match_and_substitute = [&](const string& fname,const FileCommand& cmd){
        if(std::regex_match(fname.begin(), fname.end(), cmd.glob)) {
            command_substitute(state, fname, cmd, filters);
            return true;
        }
        return false;
    };
    
    while(nftw_files.size() > 0) {
        for(const auto& fname: nftw_files) {
            for(const auto& cmd: commands) {
                if(match_and_substitute(fname, cmd))
                    break;
            }
        }
        nftw_files = state.additional_filenames;
        state.additional_filenames.clear();
    }

    // ---- Now update any environment variables (via filter products)
    for(auto& filter: filters) {
        if(!filter.products.empty()) {
            auto ii = state.env.find(filter.variable);
            std::stringstream ss("");
            bool first = true; // do we place a space...

            if(ii != state.env.end()) {
                ss << ii->second;
                first = false;
            }

            for(const auto& s: filter.products) {
                if(first) first = false;
                else ss << ' ';
                ss << s;
            }
            
            state.env[filter.variable] = ss.str();
            filter.products.clear();
        }
    }
}

// ------------------------------------------------------------- transform input

static bool transform_input(State& state)
{
    // Maximize the number of open file descriptors, so that we
    // can traverse directories accurately.
    
    vector<string> src_command;    
    
    for(string line; std::getline(state.in, line); ) {

        // Process any pending command
        if(src_command.size() > 0      // we're in a command
           && !starts_with(line, "~")  // not adding to previous command
           && !starts_with(line, "-")) // not adding to a command
            {
                process_src_command(state, src_command);
                src_command.clear();
            }

        // Process the line
        substitute_env_variables(state, line);
        if(starts_with(line, "+src")) {
            assert(src_command.size() == 0);
            src_command.push_back(line);
            
        } else if(src_command.size() > 0 &&
                  (starts_with(line, "-") || starts_with(line, "~"))) {
            src_command.push_back(line);
                        
        } else {
            state.out << line << std::endl;            
        }
    }

    if(src_command.size() > 0) {
        process_src_command(state, src_command);
        src_command.clear();
    }
            
    return true;
}
