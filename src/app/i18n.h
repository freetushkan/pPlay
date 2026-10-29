//
// UI translations: "lang/<code>.lang" files (utf-8), "english text = translation"
// lines, "@name = language name", # comments, \n \t \\ \= escapes.
//

#ifndef PPLAY_I18N_H
#define PPLAY_I18N_H

#include <string>
#include <vector>

namespace pplay {

    namespace I18n {

        struct Language {
            std::string code; // file name without extension ("ru")
            std::string name; // "@name" value, or the code
        };

        // "*.lang" directories (later ones override earlier ones)
        void setSearchPaths(const std::vector<std::string> &paths);

        // available languages, english first
        std::vector<Language> list();

        // "en" or unknown code: no translation
        bool load(const std::string &code);

        const std::string &current();

        // translation, or the text itself
        const std::string &tr(const std::string &text);
    }

    inline const std::string &tr(const std::string &text) {
        return I18n::tr(text);
    }
}

#endif //PPLAY_I18N_H
