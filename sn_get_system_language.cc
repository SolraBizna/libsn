#include "sn.hh"

// assumes already lowercase
bool SN::Context::AcceptableLanguage(const std::string& language) {
  auto it = langinfo.find(language);
  if(it != langinfo.end()) return true;
  std::string fallback_code;
  if(SN::SimpleFallback(language, fallback_code))
    return AcceptableLanguage(fallback_code);
  else
    return false;
}

static const std::array<const char*, 5> LOCALE_VARS
{"LANG","LANGSPEC","LANGUAGE","LC_MESSAGES","LC_ALL"};

std::string SN::Context::GetSystemLanguage(const std::string& default_choice) {
  MaybeGetLanguageList();
  // TODO: on Windows, use GetUserPreferredUILanguages
  for(const char* env : LOCALE_VARS) {
    auto val = getenv(env);
    if(val) {
      std::string code(val);
      auto it = code.begin();
      while(it != code.end()) {
        if(*it == '_') *it++ = '-';
        else if(*it == '.') {
          code.resize(it - code.begin());
          break;
        }
        else if(*it >= 'A' && *it <= 'Z')
          *it++ |= 0x20;
        else ++it;
      }
      if(SN::IsValidLanguageCode(code) && AcceptableLanguage(code))
        return code;
    }
  }
  return default_choice;
}
