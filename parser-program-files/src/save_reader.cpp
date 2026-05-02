#include "save_reader.hpp"
#include "utils.hpp"

#include <zlib.h>

uint16_t le16(const std::vector<unsigned char>& b, size_t off) {
    if (off + 2 > b.size()) throw std::runtime_error("ZIP read past end");
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}
uint32_t le32(const std::vector<unsigned char>& b, size_t off) {
    if (off + 4 > b.size()) throw std::runtime_error("ZIP read past end");
    return static_cast<uint32_t>(b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24));
}

std::vector<unsigned char> read_binary_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open file: " + p.string());
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(size);
    if (size) in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
}

std::string inflate_raw_deflate(const unsigned char* data, size_t compressed_size, size_t expected_size) {
    std::string out;
    out.resize(expected_size);

    z_stream zs{};
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    zs.avail_in = static_cast<uInt>(compressed_size);
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    int rc = inflateInit2(&zs, -MAX_WBITS); // ZIP uses raw deflate streams.
    if (rc != Z_OK) throw std::runtime_error("inflateInit2 failed");
    rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) throw std::runtime_error("zlib inflate failed while extracting gamestate from .sav");
    out.resize(zs.total_out);
    return out;
}

std::string extract_gamestate_from_sav(const fs::path& sav_path) {
    const auto bytes = read_binary_file(sav_path);
    if (bytes.size() < 22) throw std::runtime_error("File too small to be a ZIP/.sav: " + sav_path.string());

    // End of central directory can have a variable-length comment; search from the back.
    size_t eocd = std::string::npos;
    const size_t start = bytes.size() > (65535 + 22) ? bytes.size() - (65535 + 22) : 0;
    for (size_t i = bytes.size() - 22; i >= start; --i) {
        if (le32(bytes, i) == 0x06054b50) { eocd = i; break; }
        if (i == 0) break;
    }
    if (eocd == std::string::npos) throw std::runtime_error("Could not find ZIP central directory in .sav: " + sav_path.string());

    const uint16_t entries = le16(bytes, eocd + 10);
    const uint32_t cd_size = le32(bytes, eocd + 12);
    const uint32_t cd_offset = le32(bytes, eocd + 16);
    if (static_cast<size_t>(cd_offset) + cd_size > bytes.size()) throw std::runtime_error("Invalid central directory in .sav");

    size_t off = cd_offset;
    for (uint16_t n = 0; n < entries; ++n) {
        if (le32(bytes, off) != 0x02014b50) throw std::runtime_error("Bad ZIP central directory record");
        const uint16_t method = le16(bytes, off + 10);
        const uint32_t comp_size = le32(bytes, off + 20);
        const uint32_t uncomp_size = le32(bytes, off + 24);
        const uint16_t name_len = le16(bytes, off + 28);
        const uint16_t extra_len = le16(bytes, off + 30);
        const uint16_t comment_len = le16(bytes, off + 32);
        const uint32_t local_header = le32(bytes, off + 42);
        std::string name(reinterpret_cast<const char*>(bytes.data() + off + 46), name_len);
        off += 46 + name_len + extra_len + comment_len;

        if (name != "gamestate" && name != "meta" && name.find("gamestate") == std::string::npos) continue;
        if (name != "gamestate" && name.find("gamestate") == std::string::npos) continue;

        if (le32(bytes, local_header) != 0x04034b50) throw std::runtime_error("Bad ZIP local header for gamestate");
        const uint16_t local_name_len = le16(bytes, local_header + 26);
        const uint16_t local_extra_len = le16(bytes, local_header + 28);
        const size_t data_off = static_cast<size_t>(local_header) + 30 + local_name_len + local_extra_len;
        if (data_off + comp_size > bytes.size()) throw std::runtime_error("Compressed gamestate extends past end of .sav");

        if (method == 0) {
            return std::string(reinterpret_cast<const char*>(bytes.data() + data_off), comp_size);
        }
        if (method == 8) {
            return inflate_raw_deflate(bytes.data() + data_off, comp_size, uncomp_size);
        }
        throw std::runtime_error("Unsupported ZIP compression method for gamestate: " + std::to_string(method));
    }
    throw std::runtime_error("No gamestate entry found in .sav: " + sav_path.string());
}

// ================================================================
// Paradox key=value parser
// ================================================================

std::vector<fs::path> discover_saves(const Settings& st) {
    std::vector<fs::path> saves;
    if (!fs::exists(st.save_files_path)) throw std::runtime_error("save_files_path does not exist: " + st.save_files_path.string());
    if (st.parse_all_save_files) {
        for (const auto& ent : fs::directory_iterator(st.save_files_path)) {
            if (!ent.is_regular_file()) continue;
            const std::string filename = ent.path().filename().string();
            const std::string ext = lower_copy(ent.path().extension().string());
            if (ext != ".sav" && filename != "gamestate" && ext != ".txt") continue;
            if (st.ignore_autosaves && !(st.latest_save_only && st.latest_save_include_autosaves) && starts_with_ci(filename, "autosave")) continue;
            saves.push_back(ent.path());
        }
    } else {
        for (const auto& f : st.specific_save_files) {
            fs::path p = st.save_files_path / f;
            if (!fs::exists(p)) throw std::runtime_error("specific_save_files entry does not exist: " + p.string());
            saves.push_back(p);
        }
    }
    std::sort(saves.begin(), saves.end());
    if (st.latest_save_only && !saves.empty()) {
        const bool include_autosaves = st.latest_save_include_autosaves;
        std::vector<fs::path> latest_candidates;
        for (const auto& save : saves) {
            if (!include_autosaves && starts_with_ci(save.filename().string(), "autosave")) continue;
            latest_candidates.push_back(save);
        }
        if (latest_candidates.empty()) return {};
        auto latest_it = std::max_element(latest_candidates.begin(), latest_candidates.end(), [](const fs::path& a, const fs::path& b) {
            return fs::last_write_time(a) < fs::last_write_time(b);
        });
        return {*latest_it};
    }
    return saves;
}

std::string load_gamestate_for_save(const Settings& st, const fs::path& save_path) {
    const std::string ext = lower_copy(save_path.extension().string());
    std::string data;
    if (ext == ".sav") data = extract_gamestate_from_sav(save_path);
    else data = read_text_file(save_path);

    if (st.retain_extracted_gamestate) {
        fs::create_directories(st.retained_gamestate_path);
        fs::path out = st.retained_gamestate_path / (save_path.filename().string() + ".gamestate");
        write_text_file(out, data);
    }
    return data;
}
