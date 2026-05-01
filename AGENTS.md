\# AGENTS.md



\## Project Goal



This project parses Stellaris `.sav` files into self-contained JSON files for a future nation/planet dashboard.



The parser should eventually support dashboard views for:

\- nations/empires

\- colonized planets

\- economy and budgets

\- fleets and armies

\- demographics and species

\- political/geographic map data



\## Current Status



The parser currently:

\- builds on Windows with CMake, MSVC/NMake, and vcpkg

\- reads Stellaris `.sav` files

\- extracts `gamestate` in memory

\- parses Paradox-style key/value syntax

\- indexes major save blocks

\- exports one JSON file per selected country per save date



\## Do Not Commit



Do not commit:

\- `build/`

\- `output/`

\- `save\_files/`

\- `.sav`

\- extracted `gamestate` files

\- generated JSON output

\- local `settings.config`

\- compiler artifacts



\## Development Rules



\- Prefer small, testable changes.

\- Do not rewrite the entire parser unless explicitly requested.

\- Preserve Windows compatibility.

\- Keep `settings.example.config` generic.

\- Keep output schema backward-compatible unless intentionally bumping `schema\_version`.

\- If adding dependencies, update `vcpkg.json` and document the build impact.



\## Immediate Priorities



1\. Fix `player\_only=true` country detection.

2\. Split `main.cpp` into smaller modules.

3\. Add synthetic parser tests for Paradox save syntax.

4\. Improve x64 build defaults.

5\. Add parse/export validation summaries.

6\. Improve README setup instructions.



\## Known Bug



`player\_only=true` currently fails because Stellaris stores player info as an anonymous object inside `player={...}`:



```txt

player={

&#x20;   {

&#x20;       name="Titanic"

&#x20;       country=0

&#x20;   }

}

