// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_SCRIPT_ENVIRONMENT_HPP
#define ATP_STUDIO_SCRIPT_ENVIRONMENT_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <atp/studio/languages.hpp>
#include <atp/studio/script_modules.hpp>

namespace atp::studio {

/// The scan-path variables of every language, as the studio found them and as it rewrites them.
///
/// One object rather than a value per language, for two reasons. The inherited tail has to be captured
/// once, at startup, before the application has a second thread — a later read would already see what
/// the studio itself wrote. And every place that changes the search directories has to update *all*
/// the languages, never only the one it was thinking about: a missed language shows up as modules
/// silently absent from the palette, with nothing anywhere to read.
class script_environment {
   public:
    script_environment() {
        for (const script_language& lang : languages()) {
            inherited_.push_back(inherited_script_path(lang));
        }
    }

    /// Rewrites every language's variable from the search directories, keeping each inherited tail.
    /// @param search_dirs the manager's plugin search directories
    void apply(const std::vector<std::filesystem::path>& search_dirs) const {
        std::size_t at = 0;
        for (const script_language& lang : languages()) {
            apply_script_path(derive_script_dirs(search_dirs, lang), tail(at), lang);
            ++at;
        }
    }

    /// The value one language's variable was inherited with, for a diagnostic that has to say where
    /// the studio looked.
    /// @param lang the language in question
    /// @return the inherited value, empty when the variable was unset
    [[nodiscard]] std::string inherited(const script_language& lang) const {
        std::size_t at = 0;
        for (const script_language& one : languages()) {
            if (one.id == lang.id) {
                return tail(at);
            }
            ++at;
        }
        return {};
    }

   private:
    [[nodiscard]] std::string tail(std::size_t at) const {
        return at < inherited_.size() ? inherited_[at] : std::string();
    }

    std::vector<std::string> inherited_;
};

}  // namespace atp::studio

#endif
