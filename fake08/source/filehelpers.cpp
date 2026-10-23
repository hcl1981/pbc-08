// PBC+ port: file access goes through pfile.h (FatFs on the device) instead
// of std::ifstream, which drags in iostreams and their RAM.
#include <string>
#include <vector>

#include "filehelpers.h"
#include "pfile.h"

std::string get_file_contents(std::string filename){
    std::string contents;
    pfile *f = pf_open_read(filename.c_str());
    if (f) {
        long size = pf_size(f);
        if (size > 0) {
            contents.resize((size_t)size);
            int got = pf_read(f, &contents[0], (int)size);
            contents.resize(got > 0 ? (size_t)got : 0);
        }
        pf_close(f);
    }
    return contents;
}

std::vector<char> get_file_as_buffer(std::string filename){
    std::string s = get_file_contents(filename);
    return std::vector<char>(s.begin(), s.end());
}

std::string get_first_four_chars(std::string filename){
    std::string contents;
    pfile *f = pf_open_read(filename.c_str());
    if (f) {
        char buf[4];
        int got = pf_read(f, buf, 4);
        if (got > 0) contents.assign(buf, (size_t)got);
        pf_close(f);
    }
    return contents;
}

//https://stackoverflow.com/a/8518855
std::string getDirectory(const std::string& fname){
    size_t pos = fname.find_last_of("\\/");
    return (std::string::npos == pos)
        ? ""
        : fname.substr(0, pos);
}

bool isAbsolutePath (std::string const &path) {
    return path.length() > 0 && path[0] == '/';
}

std::string getFileExtension(std::string const &path) {
    size_t pos = path.find_last_of(".");
     return (std::string::npos == pos)
         ? ""
         : path.substr(pos);
}

static bool endsWithNoCase(std::string const &s, const char *end) {
    size_t n = 0;
    while (end[n]) n++;
    if (s.length() < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = s[s.length() - n + i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (a != end[i]) return false;
    }
    return true;
}

bool hasEnding (std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

bool isHiddenFile (std::string const &fullString) {
    return fullString.compare (0, 2, "._") == 0 || fullString.compare(0, 1, ".") == 0;
}

// FAT shows names as typed on the PC, so accept .P8 / .PNG too.
bool isCartFile (std::string const &fullString) {
    return !isHiddenFile(fullString) &&
        (endsWithNoCase(fullString, ".p8") || endsWithNoCase(fullString, ".png"));
}

bool isCPostFile (std::string const &fullString) {
    return !isHiddenFile(fullString) && 
       fullString.rfind("cpost", 0) == 0;
}
