This is a simple internationalization library. It is designed to be storage-agnostic; language files can be stored in any way appropriate for your platform and OS. It's also designed to be lean while remaining flexible.

# Building

libsn is designed to be embedded directly in other applications. There is no provision for building it as a stand-alone library.

Include the following source files in your build:

- `sn_core.cc`: Mandatory. Contains all core functionality of the library.
- `sn_get_system_language.cc`: Optional. Contains the implementation of `SN::Context::GetSystemLanguage`. You will probably want this.
- `sn_file_cat_source_posix.cc`: Optional. Contains the `SN::FileCatSource` implementation for OSes with POSIX-like paths and a `dirent` implementation. (Everything but Windows, these days.)
- `sn_file_cat_source_windows.cc`: This file **doesn't exist**, but it's where the `SN::FileCatSource` implementation for Windows would live if it did.

libsn makes use of C++14 features. Most compilers must be specially instructed to compile in C++14 mode. For gcc/clang, pass `-std=c++14`.

# Usage

This section assumes that you are familiar with good internationalization practice in general, and does not try to teach the basics.

`#include "sn.hh"`

Keep an `SN::Context` instance somewhere, conventionally in a global named `sn`.

Before use, call `sn.AddCatSource(...)`, passing some instance of `SN::CatSource`. For simple purposes, a `SN::FileCatSource` will do. (Note that there is not a Windows implementation of `SN::FileCatSource` yet.) Its constructor takes a path to a directory full of catalog files, where `<LANG>.utxt` contains the translations for the language with IETF language code `<LANG>`. libsn will get confused if a file contains a language that doesn't match its filename. If you want a different filename suffix, pass it as the second parameter to `FileCatSource`'s constructor. If the path does not contain a trailing directory separator, the final component of the path will be used as a filename prefix.

If you wish to use more than one `CatSource`, you may call `sn.AddCatSource` more than once. This might be useful for plugins, modifications, or even just for organization purposes. Call `sn.ClearCatSource` to forget all previously-added `CatSource`s. If translations for the same message are provided by more than one `CatSource`, the `CatSource` added *last* takes priority.

Call `sn.SetLanguage(...)`, passing the IETF language code you wish to use. For most purposes, you want to do `sn.SetLanguage(sn.GetSystemLanguage())`, thus selecting the best available match for the user's system language. `sn.GetSystemLanguage()` will return a default language (`en-US` unless a different code is passed as a parameter) if there are no cats available in any of the user's preferred languages.

Only the `CatSources` that were active the most recent time `sn.SetLanguage(...)` was called will take effect. If `sn.SetLanguage(...)` evaluates to false, no messages were loaded.

At this point, the context is ready for use. When you need a translated string, call `sn.Get("..."_Key)`. If the string requires substitutions, pass a braced list of substitutions as an additional parameter. If you want to output to a `std::ostream` directly, without going through a `std::string`, use `sn.Out` and pass the `ostream` as the first parameter. `sn.Get` and `sn.Out` are thread-safe.

On the rare occasion you need to fetch a translated string based on a dynamically-generated key, create an instance of `SN::ConstKey` or `SN::DynamicKey`. `ConstKey` does not own its string, whereas `DynamicKey` makes a copy of the string and owns that copy. (`_Key` is a string literal suffix that pre-computes a `ConstKey` at compile time, if, like recent GCC, your compiler is smart enough.)

On the even rarer occasion you need to detect whether a key is missing or not, you can call `sn.Lookup`, which will return `nullptr` if the key is missing. In general, you shouldn't do this; use tools to check the completeness of translations instead.

# Example

C++ source file:

    #include "sn.hh"
    #include <iostream>
    #include <string>
    
    SN::Context sn;
    
    int main(int argc, char** argv) {
        // don't forget the trailing slash!
        sn.AddCatSource(std::make_shared<SN::FileCatSource>("./"));
        if(!sn.SetLanguage(sn.GetSystemLanguage())) {
            std::cerr << "Unable to load any language files.\n";
            return 1;
        }
        std::cout << sn.Get("MESSAGE_1"_Key) << "\n";
        // MESSAGE_2 contains a trailing newline, unlike the other messages
        sn.Out(std::cout, "MESSAGE_2"_Key);
        std::cout << sn.Get("NUM_ARGS"_Key, {std::to_string(argc)}) << "\n";
        for(int n = 0; n < argc; ++n) {
            std::cout << sn.Get("ARG"_Key,
                                {std::string(argv[n]),
                                 std::to_string(n)}) << "\n";
        }
        std::cout << sn.Get("NO_MORE_ARGS"_Key) << "\n";
        std::string keystr = std::string("SIZEOF_INT_")
                             + std::to_string(sizeof(int));
        SN::ConstKey key(keystr.data(), keystr.length());
        std::cout << sn.Get(key) << "\n";
        return 0;
    }

Catalog file `en.utxt`:

    : A line beginning with a colon is a comment. Blank lines are ignored, except
    : inside a message. A line beginning with a colon, or consisting of a single
    : period, can be escaped with a leading backslash.
    
    : A message catalog begins with MIME-like headers. Unrecognized headers are
    : ignored. The file should be in whatever the application's native character
    : set is; if that character set isn't UTF-8, you'd better have a darned good
    : reason for it.
    
    : Language-Code specifies the IETF language code presented in this file. The
    : filename must match this language code.
    Language-Code: en
    : Language-Name gives the name readers of this language will know it by.
    : This will be useful in a future version of the library, which will
    : contain a way for programs to access the list of available languages.
    Language-Name: English
    : Language-Name-en gives the name an English speaker would know this language
    : by. Names in other languages can be given, too, by using different language
    : codes. This header is useful for coordinating translation efforts. If this
    : is an English catalog, the value of Language-Name is used, and this header
    : is optional.
    Language-Name-en: English
    : If a Fallback header is present, the specified language code will be used to
    : "fill in" any gaps in the catalog. For example, a British English (en_GB)
    : catalog could translate only strings where spelling differs from American
    : English (en_US), and specify en_US as the Fallback language. If no Fallback
    : header is provided, the library has some simple fallback logic which is
    : usually adequate.
    : The fallback language should ideally be one which is intelligible to readers
    : of the primary language of this catalog.
    Fallback: x-eo
    
    MESSAGE_1
    A nonblank line starts a message. Subsequent lines, up to a line containing
    only a period, form the message.
    .
    
    MESSAGE_2
    A message does not include the trailing linebreak, but can include a number of
    blank lines.
    In order for a message to contain a trailing newline, it must end with a blank
    line. (Like this message does.)
    
    .
    
    NUM_ARGS
    Value of argc is: $1
    .
    
    ARG
    Argument #$2: $1
    .
    
    NO_MORE_ARGS
    End of argument list.
    .
    
    SIZEOF_INT_2
    Your compiler has a 16-bit int.
    .
    
    SIZEOF_INT_4
    Your compiler has a 32-bit int.
    .
    
    SIZEOF_INT_8
    Your compiler has a 64-bit int.
    .
    
    __MISSING_KEY__
    If a __MISSING_KEY__ message is provided, failed lookups will use it. The
    key whose lookup failed will be passed as \$1. If no __MISSING_KEY__ is
    present in any catalog, a default obtrusive placeholder will be used.
    This message shouldn't be seen in the output of the test program, except
    maybe if the size of an int is strange.
    In case it is seen, the missing key was: $1
    .

Output:

    $ ./sntest Argument
    A nonblank line starts a message. Subsequent lines, up to a line containing
    only a period, form the message.
    A message does not include the trailing linebreak, but can include a number of
    blank lines.
    In order for a message to contain a trailing newline, it must end with a blank
    line. (Like this message does.)
    Value of argc is: 2
    Argument #0: ./sntest
    Argument #1: Argument
    End of argument list.
    Your compiler has a 32-bit int.
