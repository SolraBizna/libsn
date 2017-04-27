#ifndef SNHH
#define SNHH

#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include <initializer_list>
#include <unordered_map>

#include <string.h>

namespace SN {
  class Context;
  extern const std::string DEFAULT_LANGUAGE; // "en-US"
  bool IsValidLanguageCode(const std::string& code);
  // If there is an obvious code to fall back to, provides that code.
  //
  // Rules:
  // * If it is a language code with optional elements other than a script and
  // region, returns the language code without those optional elements.
  // * If it is a language code with a script tag, returns the language code
  // without that script tag.
  // * If it is a language code with a region tag, returns the language code
  // without that region tag.
  //
  // SN only uses this when a language is requested and there is no cat
  // available for that language. If a cat is available, fallback will only
  // take place if at least one of the cats for that language provided a
  // Fallback header. (This does not affect SimpleFallback's return value,
  // since it is not related to any context.)
  bool SimpleFallback(const std::string& from, std::string& to);
  class CatSource {
  public:
    virtual ~CatSource();
    virtual void GetAvailableCats(std::function<void(std::string)>) = 0;
    virtual std::shared_ptr<std::istream> OpenCat(const std::string& cat) = 0;
  };
  /* FileCatSource is located in sn_file_cat_source_*.cc */
  class FileCatSource : public CatSource {
    std::string dirpath_plus_prefix, dirpath, prefix, suffix;
  public:
    // The basepath will normally end with a directory separator. If it does
    // not, the last path component will end up being a filename prefix.
    FileCatSource(const std::string& basepath,
                  const std::string& suffix = ".utxt");
    virtual ~FileCatSource();
    void GetAvailableCats(std::function<void(std::string)>) override;
    std::shared_ptr<std::istream> OpenCat(const std::string& cat) override;
  };
  class Key {
  protected:
    const char* name;
    size_t name_len;
    uint32_t hash_code;
    Key() = delete;
    constexpr Key(const char* name, size_t name_len, uint32_t hash_code)
      : name(name), name_len(name_len), hash_code(hash_code) {}
  public:
    static inline constexpr uint32_t rol(uint32_t x, unsigned int n) {
      return (x << n) | (x >> (32-n));
    }
    template<class T> static inline constexpr
    uint32_t CalculateHash(T beg, T end) {
      // initial value is the hexadecimal digits to the right of the radix
      // point in pi
      return (beg >= end) ? 0x243F6A88U :
        rol(CalculateHash(beg, end-1), 5)
        // more digits are used as a scrambler
        + (static_cast<uint32_t>(*(end-1)) * 0x85A308D3U);
    }
    inline const char* GetNamePointer() const { return name; }
    inline size_t GetNameLength() const { return name_len; }
    inline uint32_t GetHashCode() const { return hash_code; }
    inline std::string AsString() const { return std::string(name,
                                                             name+name_len); }
    inline bool operator==(const Key& other) const {
      return other.hash_code == hash_code
        && other.name_len == name_len
        && !memcmp(name, other.name, name_len);
    }
    inline bool operator<(const Key& other) const {
      if(other.name_len == name_len)
        return memcmp(name, other.name, name_len) < 0;
      else if(other.name_len > name_len)
        return memcmp(name, other.name, name_len) <= 0;
      else
        return memcmp(name, other.name, other.name_len) < 0;
    }
  };
  // Does not own its string
  class ConstKey : public Key {
  public:
    constexpr ConstKey(const char* name, size_t len)
      : Key(name, len, CalculateHash(name, name+len)) {}
    constexpr ConstKey(const char* name, size_t len, uint32_t hash_code)
      : Key(name, len, hash_code) {}
    // Use this only if you are doing crazy things
    inline void UpdatePointer(const char* name) {
      this->name = name;
    }
  };
  // Owns and copies its string
  class DynamicKey : public Key {
    static const char* clone_region(const char* src, size_t len) {
      char* ret = new char[len];
      memcpy(ret, src, len);
      return ret;
    }
    void DisownName() { name = nullptr; }
  public:
    inline DynamicKey(const char* key, size_t len)
      : DynamicKey(key, len, CalculateHash(key, key+len)) {}
    inline DynamicKey(const char* key, size_t len, uint32_t hash_code)
      : Key(clone_region(key, len), len, hash_code) {}
    inline DynamicKey(const Key& other)
      : DynamicKey(other.GetNamePointer(), other.GetNameLength(),
                   other.GetHashCode()) {}
    inline DynamicKey(DynamicKey&& other)
      : Key(other.GetNamePointer(), other.GetNameLength(), other.GetHashCode())
    { other.DisownName(); }
    inline ~DynamicKey() { if(name != nullptr) delete[] name; }
    inline DynamicKey& operator=(const Key& other) {
      if(name != nullptr) delete[] name;
      name = clone_region(other.GetNamePointer(), other.GetNameLength());
      name_len = other.GetNameLength();
      hash_code = other.GetHashCode();
      return *this;
    }
    inline DynamicKey& operator=(DynamicKey&& other) {
      if(name != nullptr) delete[] name;
      name = other.GetNamePointer();
      name_len = other.GetNameLength();
      hash_code = other.GetHashCode();
      other.DisownName();
      return *this;
    }
  };
  class SubstitutableString {
    std::string storage;
    std::vector<int32_t> code;
  public:
    SubstitutableString();
    SubstitutableString(const std::string& raw);
    void operator()(Context& ctx, std::ostream& out,
                    const std::vector<std::string>&) const;
  };
}
namespace std {
  template <> struct hash<SN::ConstKey> {
    typedef decltype(reinterpret_cast<SN::Key*>(0)->GetHashCode()) result_type;
    typedef SN::ConstKey argument_type;
    result_type operator()(const argument_type& arg) const noexcept {
      return arg.GetHashCode();
    }
  };
  template <> struct hash<SN::DynamicKey> {
    typedef decltype(reinterpret_cast<SN::Key*>(0)->GetHashCode()) result_type;
    typedef SN::DynamicKey argument_type;
    result_type operator()(const argument_type& arg) const noexcept {
      return arg.GetHashCode();
    }
  };
}
namespace SN {
  class LangInfo {
    friend class Context;
    std::string code;
    bool data_loaded;
    std::string native_name;
    std::string english_name;
    std::string fallback;
  public:
    inline LangInfo(std::string code)
      : code(code), data_loaded(false) {}
    // code in standard case
    inline const std::string& GetCode() { return code; }
    // the following may be empty if no cat provided a value for them
    // language name in this language
    inline const std::string& GetNativeName() { return native_name; }
    // language name in English
    inline const std::string& GetEnglishName() { return english_name; }
    // language to fall back missing keys to
    inline const std::string& GetFallback() { return fallback; }
  };
  class Context {
    std::ostream& log;
    std::vector<std::shared_ptr<CatSource> > cat_sources;
    std::unordered_map<ConstKey, SubstitutableString> loaded_keys;
    bool langinfo_dirty;
    std::unordered_map<std::string, LangInfo> langinfo;
    const char* key_internment;
    void MaybeGetLanguageList();
    void LoadLanguage(const std::string& language,
                      std::unordered_map<std::string, SubstitutableString>&);
    bool AcceptableLanguage(const std::string& language);
    void MaybeLoadLangInfo(LangInfo& info);
  public:
    Context(std::ostream& log = std::cerr);
    ~Context();
    // erases the list of CatSources (as if newly constructed)
    Context& ClearCatSources();
    // adds a CatSource to the list
    // cats are read in order their sources were added
    // subsequent cats override previous ones, wherever more than one provides
    // the same key
    // won't actually load any cats unless SetLanguage is subsequently called
    Context& AddCatSource(const std::shared_ptr<CatSource>& loader);
    // (GetSystemLanguage is located in sn_get_system_language.cc)
    // Tries to guess the system language of the user. On Windows, this uses
    // GetUserPreferredUILanguages from the Win32 API. On all platforms, this
    // tries, in order, the LANG, LANGSPEC, LANGUAGE, LC_MESSAGES, and LC_ALL
    // environment variables. The first language code for which there are cats
    // available is returned. (This considers, for example, "es-PA" to be a
    // valid choice even if only "es" cats are available, since those "es" cats
    // will be used.)
    std::string GetSystemLanguage(const std::string& default_choice
                                  = DEFAULT_LANGUAGE);
    // clears all loaded data, sets the current language, and loads every
    // relevant cat for that language
    // if you don't call this, cats won't be loaded!
    Context& SetLanguage(const std::string& language = DEFAULT_LANGUAGE);
    // Returns true if at least one message was successfully loaded.
    operator bool() const { return !loaded_keys.empty(); }
    // Returns the SubstitutableString for the given key. You probably don't
    // want this. You probably want Get.
    const SubstitutableString* Lookup(const Key& key);
    // Returns the translated string for a given key, with the given positional
    // arguments.
    std::string Get(const Key& key,
                    std::initializer_list<std::string> args = {});
    std::string Get(const Key& key,
                    const std::vector<std::string>& args);
    void Out(std::ostream& out, const Key& key,
             std::initializer_list<std::string> args = {});
    void Out(std::ostream& out, const Key& key,
             const std::vector<std::string>& args);
  };
}

static inline constexpr SN::ConstKey operator""_Key(const char* p, size_t len){
  return SN::ConstKey(p, len, SN::Key::CalculateHash(p, p+len));
}

#endif
