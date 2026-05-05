#include "ast.hpp"
#include "cli.hpp"
#include "config.hpp"
#include "country_export.hpp"
#include "game_indexes.hpp"
#include "localization.hpp"
#include "manifest.hpp"
#include "pdx_parser.hpp"
#include "performance.hpp"
#include "save_reader.hpp"
#include "self_test.hpp"
#include "timeline_export.hpp"
#include "utils.hpp"

int main(int argc, char** argv) {
    try {
        const auto run_start = std::chrono::steady_clock::now();
        fs::path config = "settings.config";
        CliOverrides cli;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "--config" || arg == "-c") && i + 1 < argc) config = argv[++i];
            else if (arg == "--save" && i + 1 < argc) cli.save_files.push_back(argv[++i]);
            else if (arg == "--latest-save") cli.latest_save = true;
            else if (arg == "--include-autosaves") cli.include_autosaves = true;
            else if (arg == "--force-reparse") cli.force_reparse = true;
            else if (arg == "--self-test") {
                return run_parser_self_tests() ? 0 : 1;
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: stellaris_parser --config settings.config [--self-test] [--save file.sav] [--latest-save] [--include-autosaves] [--force-reparse]\n";
                return 0;
            } else {
                throw std::runtime_error("Unknown argument: " + arg);
            }
        }

        Settings st = load_settings(config);
        apply_cli_overrides(st, cli);
        std::cout << "Stellaris dashboard parser " << STELLARIS_PARSER_VERSION << "\n";
        std::cout << "Project root: " << st.project_root << "\n";
        std::cout << "Save folder:  " << st.save_files_path << "\n";
        std::cout << "Output:       " << st.output_path << "\n";

        std::optional<DefinitionIndex> defs_storage;
        const DefinitionIndex* defs = nullptr;
        if (st.parse_game_definitions) {
            std::cout << "Scanning Stellaris game definitions under " << st.stellaris_game_path << "...\n";
            defs_storage = build_definition_index(st);
            defs = &*defs_storage;
            std::cout << "Indexed " << defs->by_token.size() << " definition tokens.\n";
        }

        LocalizationDb localization_db = load_localization_db(st);
        if (localization_db.enabled) {
            if (localization_db.available) {
                std::cout << "Loaded " << localization_db.entry_count << " " << localization_db.language
                          << " localization entries from " << localization_db.source_files.size() << " files.\n";
            } else {
                std::cout << "[warn] Localization unavailable: " << localization_db.reason << "\n";
            }
        }

        auto manifest = load_manifest(st.manifest_path);
        auto saves = discover_saves(st);
        if (saves.empty()) {
            std::cout << "No save files found to parse.\n";
            return 0;
        }

        std::unordered_map<std::string, std::vector<TimelinePoint>> timeline_by_country;
        std::vector<SavePerformance> save_performances;
        RunPerformance run_perf;
        for (const fs::path& save_path : saves) {
            const auto save_start = std::chrono::steady_clock::now();
            SavePerformance save_perf;
            save_perf.save_file = save_path.filename().string();
            run_perf.saves_considered++;
            std::cout << "\n=== " << save_path.filename().string() << " ===\n";

            const auto manifest_check_start = std::chrono::steady_clock::now();
            const SaveFileMetadata save_meta = get_save_file_metadata(save_path);
            std::string skip_reason;
            if (should_skip_from_manifest(st, manifest, save_meta, &skip_reason)) {
                save_perf.skipped = true;
                save_perf.skip_reason = skip_reason;
                save_perf.manifest_check_seconds = elapsed_seconds_since(manifest_check_start);
                save_perf.save_total_seconds = elapsed_seconds_since(save_start);
                run_perf.saves_skipped++;
                run_perf.total_manifest_check_seconds += save_perf.manifest_check_seconds;
                run_perf.total_skipped_seconds += save_perf.save_total_seconds;
                std::cout << "Skipping; " << skip_reason << ".\n";
                if (st.print_performance_timings) print_save_performance(save_perf);
                save_performances.push_back(std::move(save_perf));
                continue;
            }
            save_perf.skip_reason = skip_reason;
            save_perf.manifest_check_seconds = elapsed_seconds_since(manifest_check_start);
            run_perf.saves_parsed++;
            run_perf.total_manifest_check_seconds += save_perf.manifest_check_seconds;

            std::cout << "Loading gamestate...\n";
            const auto load_start = std::chrono::steady_clock::now();
            std::string gamestate = load_gamestate_for_save(st, save_path);
            save_perf.load_gamestate_seconds = elapsed_seconds_since(load_start);
            run_perf.total_load_gamestate_seconds += save_perf.load_gamestate_seconds;
            save_perf.gamestate_bytes = gamestate.size();
            run_perf.total_gamestate_bytes += save_perf.gamestate_bytes;

            std::cout << "Parsing " << gamestate.size() << " bytes...\n";
            const auto parse_start = std::chrono::steady_clock::now();
            PdxDocument doc = parse_document(gamestate);
            save_perf.parse_document_seconds = elapsed_seconds_since(parse_start);
            run_perf.total_parse_document_seconds += save_perf.parse_document_seconds;
            gamestate.clear();
            gamestate.shrink_to_fit();

            const auto index_start = std::chrono::steady_clock::now();
            SaveIndexes ix = build_indexes(&doc.root);
            save_perf.build_indexes_seconds = elapsed_seconds_since(index_start);
            run_perf.total_build_indexes_seconds += save_perf.build_indexes_seconds;
            save_perf.indexed_countries = ix.countries.size();
            save_perf.indexed_planets = ix.planets.size();
            save_perf.indexed_species = ix.species.size();
            save_perf.indexed_fleets = ix.fleets.size();
            save_perf.indexed_armies = ix.armies.size();
            std::string game_date = scalar_or(child(&doc.root, "date"));
            std::cout << "Game date: " << game_date << "\n";
            std::cout << "Indexed countries=" << ix.countries.size() << " planets=" << ix.planets.size() << " species=" << ix.species.size() << "\n";

            const auto select_start = std::chrono::steady_clock::now();
            std::vector<std::string> selected = select_country_ids(st, ix);
            save_perf.select_countries_seconds = elapsed_seconds_since(select_start);
            run_perf.total_select_countries_seconds += save_perf.select_countries_seconds;
            save_perf.selected_countries = selected.size();
            std::cout << "Selected countries: " << selected.size() << "\n";

            ManifestEntry me;
            me.save_path = save_meta.absolute_path;
            me.save_file_name = save_meta.file_name;
            me.file_size = save_meta.file_size;
            me.last_write_time = save_meta.last_write_time;
            me.save_hash = st.manifest_skip_uses_file_metadata ? "" : file_hash_fnv1a64(save_path);
            me.settings_hash = st.settings_hash;
            me.parser_version = STELLARIS_PARSER_VERSION;
            me.game_date = game_date;
            me.parsed_at = now_iso8601();

            fs::path date_dir = st.output_path / output_date_folder(game_date);
            const auto export_start = std::chrono::steady_clock::now();
            for (const std::string& cid : selected) {
                auto it = ix.countries.find(cid);
                if (it == ix.countries.end()) {
                    std::cerr << "[warn] Country ID not found: " << cid << "\n";
                    continue;
                }
                std::string cname = get_country_name(it->second);
                fs::path out_file = date_dir / (cid + "-(" + sanitize_filename(cname) + ")_" + output_date_suffix(game_date) + ".json");
                const auto country_export_start = std::chrono::steady_clock::now();
                auto result = write_country_output(out_file, save_path.filename().string(), game_date, cid, it->second, ix, st, defs, &localization_db);
                const double country_export_seconds = elapsed_seconds_since(country_export_start);
                CountryExportSummary s = result.first;
                TimelinePoint tp = result.second;
                const auto timeline_point_start = std::chrono::steady_clock::now();
                timeline_by_country[cid].push_back(tp);
                save_perf.timeline_point_seconds += elapsed_seconds_since(timeline_point_start);

                CountryPerformance country_perf;
                country_perf.country_id = s.country_id;
                country_perf.country_name = s.country_name;
                country_perf.exported_colonies = s.exported_colonies;
                country_perf.systems_exported = s.systems_exported;
                country_perf.output_file = out_file;
                country_perf.export_seconds = country_export_seconds;
                save_perf.countries.push_back(std::move(country_perf));

                std::cout << "Save: " << game_date << "\n";
                std::cout << "Selected countries: " << selected.size() << "\n";
                std::cout << "  Country " << s.country_id << ": " << s.country_name << "\n";
                if (!s.capital_id.empty()) {
                    std::cout << "    Capital: " << s.capital_id << " / " << (s.capital_name.empty() ? "<unknown>" : s.capital_name) << "\n";
                } else {
                    std::cout << "    Capital: <none>\n";
                }
                std::cout << "    Owned planets: " << s.owned_planets << "\n";
                std::cout << "    Exported colonies: " << s.exported_colonies << "\n";
                std::cout << "    Unresolved references: " << s.unresolved_references << "\n";
                std::cout << "    Warnings: " << s.warnings << "\n";
                std::cout << "    Wrote: " << out_file << "\n";
                me.outputs.push_back(fs::absolute(out_file).string());
            }
            save_perf.export_countries_seconds = elapsed_seconds_since(export_start);
            run_perf.total_export_countries_seconds += save_perf.export_countries_seconds;
            run_perf.total_timeline_point_seconds += save_perf.timeline_point_seconds;

            // Replace older manifest entry for this save, then append the new one.
            const auto manifest_write_start = std::chrono::steady_clock::now();
            manifest.erase(std::remove_if(manifest.begin(), manifest.end(), [&](const ManifestEntry& e) {
                return e.save_path == me.save_path;
            }), manifest.end());
            manifest.push_back(std::move(me));
            save_manifest(st.manifest_path, manifest, st.pretty_json);
            save_perf.manifest_write_seconds = elapsed_seconds_since(manifest_write_start);
            run_perf.total_manifest_write_seconds += save_perf.manifest_write_seconds;
            save_perf.save_total_seconds = elapsed_seconds_since(save_start);
            if (st.print_performance_timings) print_save_performance(save_perf);
            save_performances.push_back(std::move(save_perf));
        }

        if (st.export_timeline) {
            run_perf.total_timeline_write_seconds = write_timeline_outputs(st, saves, timeline_by_country);
        }
        run_perf.total_run_seconds = elapsed_seconds_since(run_start);

        if (st.print_performance_timings) {
            print_total_performance(run_perf);
        }

        if (st.write_performance_log) {
            write_performance_log(st.output_path / "performance-log.json", save_performances, run_perf, st.pretty_json);
        }

        std::cout << "\nDone. Manifest: " << st.manifest_path << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}

