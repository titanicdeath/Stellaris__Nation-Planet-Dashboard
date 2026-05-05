#include "self_test.hpp"
#include "ast.hpp"
#include "pdx_parser.hpp"
#include "utils.hpp"

bool run_parser_self_tests() {
    struct Case { std::string name; std::string input; };
    const std::vector<Case> cases = {
        {"duplicate keys", "planet=1 planet=2 planet=3"},
        {"anonymous objects", "player={ { name=\"Titanic\" country=0 } }"},
        {"primitive lists", "owned_planets={ 2 3 4 }"},
        {"nested objects", "country={ capital=2 stats={ pops=42 } }"},
        {"quoted strings", "name=\"Tetran Sacrosanct Imperium\""},
        {"bare identifiers", "type=default ethos={ ethic_fanatic_materialist }"},
        {"yes/no bools", "is_ai=no has_gateway=yes"},
        {"empty objects", "flags={}"},
        {"player wrapper", "player={ { name=\"Titanic\" country=0 } }"},
    };
    bool ok = true;
    for (const auto& tc : cases) {
        try {
            PdxDocument doc = parse_document(tc.input);
            (void)doc;
            std::cout << "[self-test] PASS: " << tc.name << "\n";
        } catch (const std::exception& ex) {
            ok = false;
            std::cout << "[self-test] FAIL: " << tc.name << " -> " << ex.what() << "\n";
        }
    }

    struct DateCase { std::string start; std::string end; int years; };
    const std::vector<DateCase> date_cases = {
        {"2264.01.01", "2295.01.01", 31},
        {"2264.07.08", "2295.07.07", 30},
        {"2264.07.08", "2295.07.08", 31},
    };
    for (const auto& tc : date_cases) {
        auto years = years_between_stellaris_dates(tc.start, tc.end);
        if (years && *years == tc.years) {
            std::cout << "[self-test] PASS: years_between " << tc.start << " -> " << tc.end << "\n";
        } else {
            ok = false;
            std::cout << "[self-test] FAIL: years_between " << tc.start << " -> " << tc.end
                      << " expected " << tc.years << "\n";
        }
    }
    return ok;
}
