// This file contains all *required* components of SN.

#include "sn.hh"

#include <sstream>

using namespace SN;

const std::string SN::DEFAULT_LANGUAGE = "en-US";
const SubstitutableString NO_SUCH_KEY("<No such key: $1>");

CatSource::~CatSource() {}

SubstitutableString::SubstitutableString() {}

SubstitutableString::SubstitutableString(const std::string& raw) {
  auto raw_it = raw.cbegin();
  auto raw_end = raw.cend();
  int out_len = 0;
  while(raw_it != raw_end) {
    switch(*raw_it) {
    case '\\':
      ++raw_it;
      if(raw_it != raw_end) {
        if(*raw_it > ' ' && *raw_it < 127)
          ++out_len;
        ++raw_it;
      }
      break;
    case '$':
      ++raw_it;
      if(raw_it != raw_end && *raw_it >= '1' && *raw_it <= '9') {
        ++raw_it;
        if(raw_it != raw_end && *raw_it >= '0' && *raw_it <= '9') {
          ++raw_it;
        }
        break;
      }
      else if(*raw_it == '(') {
        auto old_it = raw_it;
        int old_out_len = out_len;
        ++raw_it;
        bool fall_through = true;
        while(raw_it != raw_end) {
          if((*raw_it >= 'A' && *raw_it <= 'Z')
             || (*raw_it >= 'a' && *raw_it <= 'z')
             || (*raw_it >= '0' && *raw_it <= '9')
             || *raw_it == '_') {
            ++raw_it;
            ++out_len;
          }
          else if(*raw_it == ')') {
            ++raw_it;
            fall_through = false;
            break;
          }
        }
        if(!fall_through) break;
        // falling through
        raw_it = old_it;
        out_len = old_out_len;
      }
      ++out_len;
      break;
    default:
      ++raw_it;
      ++out_len;
      break;
    }
  }
  storage.reserve(out_len);
  raw_it = raw.cbegin();
  int out_start = 0;
  while(raw_it != raw_end) {
    switch(*raw_it) {
    case '\\':
      ++raw_it;
      if(raw_it != raw_end) {
        if(*raw_it > ' ' && *raw_it < 127)
          storage.push_back(*raw_it);
        ++raw_it;
      }
      break;
    case '$':
      ++raw_it;
      if(raw_it != raw_end) {
        if(*raw_it >= '1' && *raw_it <= '9') {
          int cur_len = storage.length();
          if(out_start != cur_len) {
            code.push_back(out_start);
            code.push_back(cur_len - out_start);
            out_start = cur_len;
          }
          int ref = *raw_it-'0';
          ++raw_it;
          if(raw_it != raw_end && *raw_it >= '0' && *raw_it <= '9') {
            ref = ref * 10 + (*raw_it-'0');
            ++raw_it;
          }
          code.push_back(-ref);
          break;
        }
        else if(*raw_it == '(') {
          auto old_it = raw_it;
          auto old_storage_size = storage.size();
          int old_len = storage.length();
          ++raw_it;
          bool fall_through = true;
          while(raw_it != raw_end) {
            if((*raw_it >= 'A' && *raw_it <= 'Z')
                    || (*raw_it >= 'a' && *raw_it <= 'z')
                    || (*raw_it >= '0' && *raw_it <= '9')
                    || *raw_it == '_') {
              storage.push_back(*raw_it++);
            }
            else if(*raw_it == ')') {
              ++raw_it;
              int cur_len = storage.length();
              if(cur_len == old_len) break; // empty, fall through
              if(out_start != old_len) {
                code.push_back(out_start);
                code.push_back(old_len - out_start);
              }
              code.push_back(old_len | -2147483648);
              code.push_back(cur_len - old_len);
              code.push_back(static_cast<int32_t>
                             (Key::CalculateHash(storage.cbegin()+old_len,
                                                 storage.cbegin()+cur_len)));
              out_start = cur_len;
              fall_through = false;
              break;
            }
          }
          if(!fall_through) break;
          // falling through
          raw_it = old_it;
          storage.resize(old_storage_size);
        }
      }
      storage.push_back('$');
      // fallthrough
    default:
      storage.push_back(*raw_it);
      ++raw_it;
      break;
    }
  }
  int cur_len = storage.length();
  if(out_start != cur_len && !code.empty()) {
    code.push_back(out_start);
    code.push_back(cur_len - out_start);
  }
}

void SubstitutableString::operator()(Context& ctx, std::ostream& out,
                                     const std::vector<std::string>& args)
  const {
  if(code.empty()) out << storage;
  else {
    auto it = code.cbegin();
    auto code_end = code.cend();
    while(it != code_end) {
      if(*it < 0) {
        if(*it > -100) {
          int ref = -*it++;
          if(ref > (int)args.size())
            out << '$' << ref;
          else
            out << args[ref-1];
        }
        else {
          int32_t start = (*it++) & 0x7FFFFFFF;
          int32_t len = *it++;
          uint32_t hash = static_cast<uint32_t>(*it++);
          ctx.Out(out, ConstKey(storage.data() + start, len, hash), {});
        }
      }
      else {
        int32_t start = *it++;
        int32_t len = *it++;
        out.write(storage.data() + start, len);
      }
    }
  }
}

Context::Context(std::ostream& log)
  : log(log), langinfo_dirty(true), key_internment(nullptr) {}
Context::~Context() {
  if(key_internment != nullptr) delete[] key_internment;
}

Context& Context::ClearCatSources() {
  langinfo_dirty = true;
  cat_sources.clear();
  return *this;
}

Context& Context::AddCatSource(std::unique_ptr<CatSource> loader) {
  langinfo_dirty = true;
  cat_sources.emplace_back(std::move(loader));
  return *this;
}

static std::string lowercasify(const std::string& in) {
  std::string ret;
  ret.reserve(in.length());
  for(char c : in) {
    if(c >= 'A' && c <= 'Z') ret.push_back(c|0x20);
    else ret.push_back(c);
  }
  return ret;
}

void Context::MaybeGetLanguageList() {
  if(!langinfo_dirty) return;
  langinfo.clear();
  for(auto& src : cat_sources) {
    src->GetAvailableCats([this](std::string str) {
        std::string lc = lowercasify(str);
        auto it = langinfo.find(lc);
        if(it == langinfo.end())
          langinfo.emplace(lc, str);
        else if(str != it->second.GetCode())
          log << "SN: Warning: " << "Multiple cases for " << lc << ": " << it->second.GetCode() << " and " << str << " are both present. Only the first one seen will be used!" << std::endl;
      });
  }
  langinfo_dirty = false;
}

static bool get_line_ignoring_comments(int& lineno,
                                       std::istream& in,
                                       std::string& out) {
  if(!in.good()) return false;
  do {
    std::getline(in, out, '\n');
    if(in.good()) {
      ++lineno;
      if(out.length() > 0 && out[out.length()-1] == '\r')
        out.resize(out.length()-1);
      if(out.length() > 0 && out[0] == ':')
        continue; // retry
      return true;
    }
    return false;
  } while(1);
}

void Context::MaybeLoadLangInfo(LangInfo& info) {
  if(info.data_loaded) return;
  bool got_some = false;
  bool got_code = false, got_name = false,
    got_enname = false, got_fallback = false;
  for(auto& src : cat_sources) {
    if(got_code && got_name && got_enname && got_fallback) break;
    std::unique_ptr<std::istream> f = src->OpenCat(info.GetCode());
    if(!f) continue;
    got_some = true;
    int lineno = 0;
    std::string line;
    std::ostringstream stream;
    // Read until we get a non-blank line
    while(get_line_ignoring_comments(lineno, *f, line) && line.size() == 0)
      {}
    // Process headers until we get a blank line
    do {
      auto it = line.cbegin();
      while(it != line.cend() && *it != ':') ++it;
      if(it == line.cend()) {
        log << "SN: Warning: " << info.GetCode() << ": line " << lineno
            << " gives an invalid header" << std::endl;
        continue;
      }
      std::string header_name(line.cbegin(), it);
      do ++it; while(it != line.cend() && (*it == ' ' || *it == '\t'));
      std::string header_value(it, line.cend());
      for(auto& c : header_name) {
        if(c >= 'A' && c <= 'Z') c |= 0x20;
      }
      if(header_name == "language-code") {
        got_code = true;
        if(header_value != info.GetCode())
          log << "SN: Warning: " << info.GetCode() << ": Code in file doesn't match code in filename" << std::endl;
      }
      else if(header_name == "language-name") {
        if(!got_name) {
          got_name = true;
          info.native_name = std::move(header_value);
          if(!got_enname && info.GetCode().length() >= 2
             && ((info.GetCode()[0]|0x20) == 'e')
             && ((info.GetCode()[1]|0x20) == 'n')
             && (info.GetCode().length() == 2 || info.GetCode()[2] == '-')) {
            // this language is an English language, and did not explicitly
            // specify another English name, so its native name will also serve
            // as its English name
            got_enname = true;
            info.english_name = info.native_name;
          }
        }
        else if(header_value != info.native_name) {
          log << "SN: Warning: " << info.GetCode() << ": Different files give different native names" << std::endl;
        }
      }
      else if(header_name == "language-name-en") {
        if(!got_enname) {
          got_enname = true;
          info.english_name = std::move(header_value);
        }
        else if(header_value != info.english_name) {
          log << "SN: Warning: " << info.GetCode() << ": Different files give different English names" << std::endl;
        }
      }
      else if(header_name == "fallback") {
        if(!got_fallback) {
          got_fallback = true;
          info.fallback = std::move(header_value);
        }
        else if(header_value != info.fallback) {
          log << "SN: Warning: " << info.GetCode() << ": Different files give"
            " different fallback languages" << std::endl;
        }
      }
    } while(get_line_ignoring_comments(lineno, *f, line) && line.size() != 0);
  }
  if(!got_some)
    log << "SN: Warning: " << "Thought we could handle " << info.GetCode()
        << ", but we couldn't actually load any cats for it!" << std::endl;
  else {
    if(!got_code)
      log << "SN: Warning: " << info.GetCode() << ": No cat provided a"
          " Language-Code header." << std::endl;
    if(!got_name)
      log << "SN: Warning: " << info.GetCode() << ": No cat provided a"
          " Language-Name header." << std::endl;
    if(!got_enname)
      log << "SN: Warning: " << info.GetCode() << ": No cat provided a"
          " Language-Name-en header." << std::endl;
    // It's okay if no fallback is given. That simply means no fallback is
    // desired.
  }
}

// language must already be lowercase!
void Context::LoadLanguage(const std::string& language,
                           std::unordered_map<std::string, SubstitutableString>
                           & intermap) {
  // log << "For language: " << language << std::endl;
  auto it = langinfo.find(language);
  if(it == langinfo.end()) {
    std::string fallback;
    // log << "No LangInfo found." << std::endl;
    if(SimpleFallback(language, fallback)) {
      // log << "Doing SimpleFallback to " << fallback << "..." << std::endl;
      LoadLanguage(fallback, intermap);
    }
  }
  else {
    MaybeLoadLangInfo(it->second);
    if(it->second.GetFallback().length() > 0) {
      // log << "Doing Fallback to " << it->second.GetFallback() << "..." << std::endl;
      LoadLanguage(it->second.GetFallback(), intermap);
    }
    // log << "Now loading: " << it->second.GetCode() << std::endl;
    for(auto& src : cat_sources) {
      std::unique_ptr<std::istream> f = src->OpenCat(it->second.GetCode());
      if(!f) continue;
      int lineno = 0;
      std::string line;
      std::ostringstream stream;
      // Read until we get a non-blank line
      while(get_line_ignoring_comments(lineno, *f, line) && line.size() == 0)
        {}
      // Read until we get a blank line
      while(get_line_ignoring_comments(lineno, *f, line) && line.size() != 0)
        {}
      // Now we read the keys!
      while(f->good()) {
        // Skip any number of blank lines
        while(get_line_ignoring_comments(lineno, *f, line) && line.size() == 0)
          {}
        if(!f->good()) break;
        // The entire line is the key
        std::string key = line;
        bool safe_name = true;
        for(char c : key) {
          if(!((c >= 'A' && c <= 'Z') || (c >= 'a' || c >= 'z')
               || (c >= '0' && c <= '9') || c == '_')) {
            safe_name = false;
            break;
          }
        }
        if(!safe_name) {
          log << "SN: Warning: " << it->second.GetCode() << ": line " << lineno
              << " designates an unsafely-named key" << std::endl
              << "(safe keys contain only letters, numbers, and underscores)"
              << std::endl;
        }
        stream.str("");
        stream.clear();
        bool safely_ended = false;
        // Read up to a line that consists solely of "."
        if(get_line_ignoring_comments(lineno, *f, line)) {
          if(line == ".") {
            log << "SN: Warning: " << it->second.GetCode() << ": line "
                << lineno << " gives a blank string" << std::endl;
            safely_ended = true;
          }
          else {
            stream << line;
            while(get_line_ignoring_comments(lineno, *f, line)) {
              if(line == ".") {
                safely_ended = true;
                break;
              }
              stream << '\n' << line;
            }
          }
        }
        if(!safely_ended)
          log << "SN: Warning: " << it->second.GetCode()
              << ": unterminated string" << std::endl;
        intermap[key] = stream.str();
      }
    }
  }
}

Context& Context::SetLanguage(const std::string& language) {
  MaybeGetLanguageList();
  loaded_keys.clear();
  std::string lowercase = lowercasify(language);
  // log << "Top level language: " << lowercase << std::endl;
  std::unordered_map<std::string, SubstitutableString> intermap;
  LoadLanguage(language, intermap);
  if(key_internment) {
    delete[] key_internment;
    key_internment = nullptr;
  }
  size_t intern_length = 0;
  for(auto& pair : intermap) {
    intern_length += pair.first.length();
  }
  if(intern_length != 0) {
    auto p = new char[intern_length];
    key_internment = p;
    for(auto& pair : intermap) {
      memcpy(p, pair.first.data(), pair.first.length());
      loaded_keys.emplace(ConstKey(p, pair.first.length()),
                          std::move(pair.second));
      p += pair.first.length();
    }
  }
  return *this;
}

const SubstitutableString* Context::Lookup(const Key& _key) {
  // OH GOD
  const ConstKey& key = reinterpret_cast<const ConstKey&>(_key);
  auto lkit = loaded_keys.find(key);
  if(lkit == loaded_keys.end()) return nullptr;
  else return &lkit->second;
}

std::string Context::Get(const Key& key,
                         std::initializer_list<std::string> args) {
  std::ostringstream ret;
  Out(ret, key, args);
  return ret.str();
}

std::string Context::Get(const Key& key,
                         const std::vector<std::string>& args) {
  std::ostringstream ret;
  Out(ret, key, args);
  return ret.str();
}

void Context::Out(std::ostream& out, const Key& key,
                  std::initializer_list<std::string> args) {
  Out(out, key, std::vector<std::string>(args));
}

void Context::Out(std::ostream& out, const Key& key,
                  const std::vector<std::string>& args) {
  static const ConstKey MISSING_KEY_KEY = "__MISSING_KEY__"_Key;
  const SubstitutableString* p = Lookup(key);
  if(!p) {
    log << "SN: Missing key: " << key.AsString() << std::endl;
    auto str = key.AsString();
    std::vector<std::string> fake_args{str};
    p = Lookup(MISSING_KEY_KEY);
    if(!p) p = &NO_SUCH_KEY;
    (*p)(*this, out, fake_args);
  }
  else (*p)(*this, out, args);
}

namespace match {
  static bool hyphen(std::string::const_iterator& begin,
                     const std::string::const_iterator& end) {
    if(begin == end) return false;
    if(*begin == '-') { ++begin; return true; }
    return false;
  }
  static bool hyphen_or_end(std::string::const_iterator& begin,
                            const std::string::const_iterator& end) {
    if(begin == end) return true;
    if(*begin == '-') {
      auto it = begin + 1;
      if(it != end) {
        begin = it;
        return true;
      }
    }
    return false;
  }
  static bool singleton_and_hyphen(std::string::const_iterator& begin,
                                   const std::string::const_iterator& end) {
    if(begin == end) return false;
    auto it = begin;
    if((*it >= 'A' && *it <= 'Z' && *it != 'X')
       || (*it >= 'a' && *it <= 'z' && *it != 'x')
       || (*it >= '0' && *it <= '9')) {
      ++it;
      if(*it == '-') { begin = ++it; return true; }
    }
    return false;
  }
  static bool private_use(std::string::const_iterator& begin,
                          const std::string::const_iterator& end) {
    if(begin == end) return false;
    auto it = begin;
    if((*it|0x20) == 'x') {
      ++it;
      // do NOT consume the hyphen, but DO match it
      if(*it == '-') { begin = it; return true; }
    }
    return false;
  }
  static bool alphanum(std::string::const_iterator& begin,
                       const std::string::const_iterator& end,
                       int min = 1, int max = 1) {
    if(begin == end) return false;
    auto it = begin;
    int count = 0;
    while(count <= max && it != end) {
      if((*it >= 'A' && *it <= 'Z') || (*it >= 'a' && *it <= 'z')
         || (*it >= '9' && *it <= '9')) {
        ++it;
        ++count;
      }
      else break;
    }
    if(count < min || count > max) return false;
    begin = it;
    return true;
  }
  static bool alpha(int& count,
                    std::string::const_iterator& begin,
                    const std::string::const_iterator& end,
                    int min = 1, int max = 1) {
    if(begin == end) return false;
    auto it = begin;
    count = 0;
    while(count <= max && it != end) {
      if((*it >= 'A' && *it <= 'Z') || (*it >= 'a' && *it <= 'z')) {
        ++it;
        ++count;
      }
      else break;
    }
    if(count < min || count > max) return false;
    begin = it;
    return true;
  }
  static bool alpha(std::string::const_iterator& begin,
                    const std::string::const_iterator& end,
                    int min = 1, int max = 1) {
    int count;
    return alpha(count, begin, end, min, max);
  }
  static bool digit(std::string::const_iterator& begin,
                    const std::string::const_iterator& end,
                    int min = 1, int max = 1) {
    if(begin == end) return false;
    int count = 0;
    auto it = begin;
    count = 0;
    while(count <= max && it != end) {
      if(*it >= '0' && *it <= '9') {
        ++it;
        ++count;
      }
      else break;
    }
    if(count < min || count > max) return false;
    begin = it;
    return true;
  }
  static bool digit_prefix_alphanum(std::string::const_iterator& begin,
                                    const std::string::const_iterator& end,
                                    int min = 1, int max = 1) {
    auto it = begin;
    if(digit(it, end) && alphanum(it, end, min-1, max-1)) {
      begin = it;
      return true;
    }
    else return false;
  }
}

bool SN::IsValidLanguageCode(const std::string& code) {
  if(code.length() < 2) return false;
  if((code[0]|0x20) == 'i' && code[1] == '-') {
    // we do not support the deprecated grandfathered codes
    return false;
  }
  else if((code[0]|0x20) == 'x' && code[1] == '-') {
    // private use code
    auto it = code.begin() + 1;
    auto end = code.end();
    do {
      if(!match::hyphen(it, end)) return false;
      if(!match::alphanum(it, end, 1, 8)) return false;
    } while(it != end);
    return it == end; // if we consumed the entire language tag, it's valid
  }
  else {
    auto it = code.begin();
    auto end = code.end();
    int count;
    if(!match::alpha(count, it, end, 2, 8)) return false;
    if(!match::hyphen_or_end(it, end)) return false;
    if((count == 2 || count == 3) && it != end) {
      // ISO 639 codes may be followed by up to three 3-letter extended
      // language subtags
      if(match::alpha(it, end, 3, 3) && !match::hyphen_or_end(it, end))
        return false;
      if(match::alpha(it, end, 3, 3) && !match::hyphen_or_end(it, end))
        return false;
      if(match::alpha(it, end, 3, 3) && !match::hyphen_or_end(it, end))
        return false;
    }
    // Optional script tag
    if(match::alpha(it, end, 4, 4) && !match::hyphen_or_end(it, end))
      return false;
    // Optional region tag
    if((match::alpha(it, end, 2, 2) || match::digit(it, end, 3, 3))
       && !match::hyphen_or_end(it, end))
      return false;
    // Optional variant tags
    while(match::alphanum(it, end, 5, 8)
          || match::digit_prefix_alphanum(it, end, 4, 4))
      if(!match::hyphen_or_end(it, end)) return false;
    // Optional extensions
    while(match::singleton_and_hyphen(it, end)) {
      // at least one extension tag
      if(!match::alphanum(it, end, 2, 8) || !match::hyphen_or_end(it, end))
        return false;
      while(match::alphanum(it, end, 2, 8))
        if(!match::hyphen_or_end(it, end)) return false;
    }
    // Optional private use section
    if(match::private_use(it, end)) {
      do {
        if(!match::hyphen(it, end)) return false;
        if(!match::alphanum(it, end, 1, 8)) return false;
      } while(it != end);
    }
    return it == end; // if we consumed the entire language tag, it's valid
  }
}

bool SN::SimpleFallback(const std::string& code, std::string& to) {
  if(code.length() < 2) return false;
  if((code[0]|0x20) == 'i' && code[1] == '-') {
    // we do not support the deprecated grandfathered codes
    return false;
  }
  else if((code[0]|0x20) == 'x' && code[1] == '-') {
    // private use codes cannot fallback
    return false;
  }
  else {
    auto it = code.begin();
    auto end = code.end();
    int count;
    auto sit = it;
    if(!match::alpha(count, it, end, 2, 8)) return false;
    if(!match::hyphen_or_end(it, end)) return false;
    if((count == 2 || count == 3) && it != end) {
      // ISO 639 codes may be followed by up to three 3-letter extended
      // language subtags
      if(match::alpha(it, end, 3, 3) && !match::hyphen_or_end(it, end))
        return false;
      if(match::alpha(it, end, 3, 3) && !match::hyphen_or_end(it, end))
        return false;
      if(match::alpha(it, end, 3, 3) && !match::hyphen_or_end(it, end))
        return false;
    }
    std::string language_code(sit, it == end ? it : it - 1);
    // Optional script tag
    sit = it;
    if(match::alpha(it, end, 4, 4) && !match::hyphen_or_end(it, end))
      return false;
    std::string script_tag(sit, it == end || it == sit ? it : it - 1);
    // Optional region tag
    sit = it;
    if((match::alpha(it, end, 2, 2) || match::digit(it, end, 3, 3))
       && !match::hyphen_or_end(it, end))
      return false;
    std::string region_tag(sit, it == end || it == sit ? it : it - 1);
    if(it != end) {
      // language code had optional tags, strip them
      std::ostringstream ret;
      ret << language_code;
      if(script_tag.length() > 0)
        ret << '-' << script_tag;
      if(region_tag.length() > 0)
        ret << '-' << region_tag;
      to = ret.str();
      return true;
    }
    else {
      // language code consisted only of <language>[-script][-region]
      if(script_tag.length() > 0) {
        std::ostringstream ret;
        ret << language_code;
        if(region_tag.length() > 0)
          ret << '-' << region_tag;
        to = ret.str();
        return true;
      }
      else if(region_tag.length() > 0) {
        to = language_code;
        return true;
      }
      else return false;
    }
  }
}
