#include "sn.hh"

#include <dirent.h>
#include <fstream>

SN::FileCatSource::FileCatSource(const std::string& basepath,
                                 const std::string& suffix)
  : dirpath_plus_prefix(basepath), suffix(suffix) {
  auto it = basepath.cbegin();
  auto slash = basepath.cend();
  while(it != basepath.cend()) {
    if(*it == '/') slash = it;
    ++it;
  }
  if(slash == basepath.cend()) {
    // No slashes in path. Directory will be "."
    dirpath = ".";
    prefix = basepath;
  }
  else {
    ++slash;
    dirpath = std::string(basepath.cbegin(), slash);
    prefix = std::string(slash, basepath.cend());
  }
}

SN::FileCatSource::~FileCatSource() {}

void
SN::FileCatSource::GetAvailableCats(std::function<void(std::string)> func) {
  DIR* d = opendir(dirpath.c_str());
  if(d) {
    struct dirent* ent;
    while((ent = readdir(d))) {
      if(ent->d_name[0] == '.'
#ifdef DT_REG
         || ent->d_type != DT_REG
#endif
         ) continue;
      std::string name(ent->d_name);
      if(name.length() <= prefix.length() + suffix.length()) continue;
      if(name.compare(0, prefix.length(), prefix) != 0) continue;
      if(name.compare(name.length()-suffix.length(), name.length(), suffix)!=0)
        continue;
      std::string code(name.begin()+prefix.length(),
                       name.begin()+(name.length()-suffix.length()));
      for(auto& c : code) {
        if(c == '-') continue;
        else if(c == '_') c = '-';
      }
      if(SN::IsValidLanguageCode(code)) func(std::move(code));
    }
    closedir(d);
  }
}

std::unique_ptr<std::istream>
SN::FileCatSource::OpenCat(const std::string& cat) {
  std::string code(cat);
  for(auto& c : code)
    if(c == '-') c = '_';
  std::string path;
  path.reserve(dirpath_plus_prefix.length() + code.length() + suffix.length());
  ((path += dirpath_plus_prefix) += code) += suffix;
  auto ret = std::make_unique<std::fstream>
    (path, std::ios::binary|std::ios::in);
  if(!ret->good()) return nullptr;
  else return ret;
}
