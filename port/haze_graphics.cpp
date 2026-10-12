#include "haze_graphics.h"
#include "rsx_graphics_settings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <map>

namespace {
std::string trim(std::string text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if (first==std::string::npos) return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
}
bool haze_graphics_initialize() {
    const char* configured=std::getenv("HAZE_GRAPHICS_CONFIG");
    const char* path=configured && *configured ? configured : "config/graphics.ini";
    std::ifstream file(path);
    if (!file && configured && *configured) {
        std::fprintf(stderr,"[graphics] Cannot open %s\n",path); return false;
    }
    std::map<std::string,std::string> values;
    std::string line,section;
    unsigned number=0;
    while (std::getline(file,line)) {
        ++number;
        if (number==1 && line.compare(0,3,"\xef\xbb\xbf")==0) line.erase(0,3);
        line=trim(line.substr(0,line.find_first_of(";#")));
        if (line.empty()) continue;
        if (line.front()=='[' && line.back()==']') {
            section=trim(line.substr(1,line.size()-2));
            if (section=="graphics") continue;
            std::fprintf(stderr,"[graphics] %s:%u: unknown section '%s'\n",path,number,section.c_str()); return false;
        }
        const auto split=line.find('=');
        const auto key=trim(line.substr(0,split));
        const rsx_graphics_option* option=rsx_graphics_find_option(key.c_str());
        if (section!="graphics" || split==std::string::npos || !option || values.count(key)) {
            std::fprintf(stderr,"[graphics] %s:%u: unknown section/key or duplicate setting\n",path,number); return false;
        }
        auto value=trim(line.substr(split+1));
        if (!rsx_graphics_valid(option,value.c_str())) {
            std::fprintf(stderr,"[graphics] %s:%u: invalid %s=%s (expected %s)\n",path,number,key.c_str(),value.c_str(),option->allowed); return false;
        }
        values.emplace(key,value);
    }
    if (file.bad()) { std::fprintf(stderr,"[graphics] Error reading %s\n",path); return false; }
    // Validate the complete file and overrides before changing the process.
    for (const auto& option:rsx_graphics_options) {
        const char* value=std::getenv(option.environment);
        if (value && *value && !rsx_graphics_valid(&option,value)) {
            std::fprintf(stderr,"[graphics] Invalid %s=%s (expected %s)\n",option.environment,value,option.allowed); return false;
        }
    }
    for (const auto& option:rsx_graphics_options) {
        const char* current=std::getenv(option.environment);
        const auto found=values.find(option.key);
        const std::string value=current && *current ? current :
            found!=values.end() ? found->second.c_str() : option.fallback;
#ifdef _WIN32
        if (_putenv_s(option.environment,value.c_str())!=0) return false;
#else
        if (setenv(option.environment,value.c_str(),1)!=0) return false;
#endif
        std::fprintf(stderr,"[graphics] %s=%s\n",option.key,value.c_str());
    }
    return true;
}
