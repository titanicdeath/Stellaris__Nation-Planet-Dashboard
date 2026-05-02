#include "cli.hpp"

void apply_cli_overrides(Settings& st, const CliOverrides& cli) {
    if (!cli.save_files.empty()) {
        st.parse_all_save_files = false;
        st.specific_save_files = cli.save_files;
        st.latest_save_only = false;
    }
    if (cli.latest_save) {
        st.parse_all_save_files = true;
        st.specific_save_files.clear();
        st.latest_save_only = true;
    }
    if (cli.include_autosaves) {
        st.ignore_autosaves = false;
        st.latest_save_include_autosaves = true;
    }
    if (cli.force_reparse) {
        st.force_reparse = true;
    }
}
