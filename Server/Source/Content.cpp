#include "Content.hpp"

#include "Maps.hpp"
#include "Server.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>

namespace lsfs {

using namespace lsf::proto;

ServerContent::~ServerContent() { lsf::pack::remove_layer(layer_); }

void ServerContent::unload(MapCache& maps) {
    if (registry_set_) lsf::registry::clear();
    registry_set_ = false;
    if (layer_.empty()) return;
    maps.mount({});
    lsf::pack::remove_layer(layer_);
    layer_.clear();
}

bool ServerContent::load(const std::filesystem::path& dir, MapCache& maps, std::string* why, lsf::u32 evil) {
    packs_.clear();
    manifest_.clear();
    manifest_hash_.clear();
    unload(maps);
    if (evil & (kEvilUnchecked | kEvilOversize | kEvilRegistryLies)) {
        // A hostile server (tests): whatever lies in packs/, announced as it pleases.
        std::vector<std::filesystem::path> raw;
        std::error_code rec;
        for (const auto& e : std::filesystem::directory_iterator(dir, rec))
            if (e.path().extension() == ".lsfpack") raw.push_back(e.path());
        std::sort(raw.begin(), raw.end());
        std::vector<std::pair<lsf::pack::Pack, std::string>> named;
        std::vector<lsf::u32> sizes;
        for (const auto& f : raw) {
            auto bytes = eng::fs::read_file(f);
            if (!bytes) continue;
            lsf::pack::Pack p;
            if (!lsf::pack::read(*bytes, p, nullptr, false)) {
                p = lsf::pack::Pack{};
                p.id = eng::str::narrow(f.stem().wstring());
                p.definitions = "{}";
            }
            const std::string sha = lsf::pack::sha256_hex(*bytes);
            named.emplace_back(p, sha);
            sizes.push_back((evil & kEvilOversize) ? lsf::u32(60u << 20) : lsf::u32(bytes->size()));
            packs_.push_back({p.id, sha, lsf::u32(bytes->size()), f, std::move(*bytes)});
        }
        eng::json::Value m = lsf::pack::manifest(named, sizes);
        if (evil & kEvilRegistryLies) {
            eng::json::Value weapons = eng::json::Value::array();
            for (eng::json::Value w : m["weapons"].elements()) {
                w["damage"] = 499;
                weapons.push(std::move(w));
            }
            m["weapons"] = std::move(weapons);
        }
        const std::string text = m.dump();
        manifest_.assign(text.begin(), text.end());
        if (!packs_.empty()) manifest_hash_ = lsf::pack::sha256_hex(manifest_);
        return true;
    }
    std::vector<lsf::pack::Loaded> loaded;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (!dir.empty() && std::filesystem::is_directory(dir, ec)) {
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".lsfpack") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        lsf::u64 total = 0;
        for (const auto& f : files) {
            const std::string name = eng::str::narrow(f.filename().wstring());
            total += lsf::u64(std::filesystem::file_size(f, ec));
            // DL-6: 50 MB of packs, all together -- said before any is read.
            if (total > lsf::pack::kMaxPackBytes) {
                if (why) *why = "the packs come to more than 50 MB together (DL-6)";
                return false;
            }
            lsf::pack::Loaded l;
            std::string bad;
            if (!lsf::pack::load_file(f, l, &bad)) {
                if (why) *why = name + ": " + bad;
                return false;
            }
            loaded.push_back(std::move(l));
        }
    }
    // PK-4: the whole set, against the client's own names when the server has its data.
    lsf::pack::BaseIndex index;
    if (!loaded.empty() && maps.available()) index.build(maps.data());
    if (!lsf::pack::check_set(loaded, index.built() ? index.lookup() : lsf::pack::BaseLookup(), why)) return false;
    std::vector<std::pair<lsf::pack::Pack, std::string>> named;
    std::vector<lsf::u32> sizes;
    for (size_t i = 0; i < loaded.size(); ++i) {
        named.emplace_back(loaded[i].pack, loaded[i].sha256);
        sizes.push_back(lsf::u32(loaded[i].bytes.size()));
    }
    const eng::json::Value m = lsf::pack::manifest(named, sizes);
    const std::string text = m.dump();
    manifest_.assign(text.begin(), text.end());
    // PK-9: a manifest goes out in at most 64 pieces.
    if (manifest_.size() > 64 * kManifestPiece) {
        if (why) *why = "the packs define more than a manifest can carry";
        return false;
    }
    lsf::SessionContent content;
    std::string bad;
    if (!lsf::pack::registry_from_manifest(m, content, &bad)) {
        if (why) *why = "the manifest: " + bad;
        return false;
    }
    // The maps they add are read from the packs' own files, laid over the data folder (MT-1); and
    // what the definitions say of the models is measured, not believed.
    if (!loaded.empty() && maps.available()) {
        lsf::u8 salt[4];
        eng::crypto::random_bytes(salt);
        layer_ = std::filesystem::temp_directory_path(ec) / eng::str::format("lsf-packs-%02x%02x%02x%02x", salt[0], salt[1], salt[2], salt[3]);
        if (!lsf::pack::write_layer(layer_, loaded, &bad)) {
            if (why) *why = bad;
            return false;
        }
        maps.mount(layer_);
        if (!lsf::pack::check_mounted(maps.data(), content, &bad)) {
            if (why) *why = bad;
            unload(maps);
            return false;
        }
    }
    for (size_t i = 0; i < loaded.size(); ++i)
        packs_.push_back({loaded[i].pack.id, loaded[i].sha256, lsf::u32(loaded[i].bytes.size()), files[i], std::move(loaded[i].bytes)});
    // No packs: no manifest to download (the hash stays empty, and Ready names it so).
    if (!packs_.empty()) manifest_hash_ = lsf::pack::sha256_hex(manifest_);
    content.manifest_hash = manifest_hash_;
    // A server with no packs adds nothing, and leaves the registry to whoever else is in this
    // process (the game hosting on This PC, joined to a server of its own).
    if (!packs_.empty()) {
        lsf::registry::set(std::move(content));
        registry_set_ = true;
    }
    if (!packs_.empty()) LOG_INFO("Packs: %zu, %.1f MB, manifest %s", packs_.size(), double(total_bytes()) / 1048576.0, manifest_hash_.substr(0, 12).c_str());
    return true;
}

lsf::u64 ServerContent::total_bytes() const {
    lsf::u64 n = 0;
    for (const ServerPack& p : packs_) n += p.size;
    return n;
}

const ServerPack* ServerContent::pack(std::string_view sha256) const {
    for (const ServerPack& p : packs_)
        if (p.sha256 == sha256) return &p;
    return nullptr;
}

std::vector<std::string> ServerContent::heartbeat_list() const {
    std::vector<std::string> out;
    for (const ServerPack& p : packs_) out.push_back(p.id + ":" + p.sha256);
    return out;
}

// ── The manifest and the downloads (§11.6, §11.7) ──────────────────────────────

void Server::send_manifest(Peer& p) {
    // No packs: one empty piece, and the game goes straight on to Ready.
    static const std::vector<u8> none;
    const auto& m = content_.packs().empty() ? none : content_.manifest();
    const size_t pieces = std::max<size_t>(1, (m.size() + kManifestPiece - 1) / kManifestPiece);
    for (size_t i = 0; i < pieces; ++i) {
        ManifestPart part;
        part.index = u16(i);
        part.count = u16(pieces);
        const size_t from = i * kManifestPiece, n = std::min(kManifestPiece, m.size() - std::min(m.size(), from));
        part.bytes.assign(m.begin() + std::ptrdiff_t(from), m.begin() + std::ptrdiff_t(from + n));
        // A hostile server (tests, SC-5): more pieces than a manifest has, or not the one announced.
        if (options_.test_evil & kEvilManifestHuge) part.count = 65;
        if ((options_.test_evil & kEvilManifestOther) && !part.bytes.empty()) part.bytes[part.bytes.size() / 2] ^= 0x20;
        send_msg(p.id, part);
    }
}

void Server::content_request(Peer& p, const ContentRequest& m) {
    if (m.cancel) {
        p.download.reset();
        return;
    }
    const ServerPack* pack = content_.pack(m.sha256);
    ContentChunk err;
    err.sha256 = m.sha256;
    if (!pack) {
        err.error = "This server has no such pack.";
        send_msg(p.id, err);
        return;
    }
    if (m.offset > pack->size) {
        err.error = "That offset is past the pack's end.";
        send_msg(p.id, err);
        return;
    }
    p.download = Peer::Download{pack->sha256, m.offset, now_};
    p.waiting_since = now_;
}

// DL-1: chunks go only while a downloader's reliable queue is short; no more than
// `max_downloaders` at once, the rest waiting their turn (told their place); the whole upload held
// to `upload_kbps`, so a match's own traffic never waits behind a download.
void Server::pump_downloads(double now) {
    std::vector<Peer*> want;
    for (auto& [id, p] : peers_)
        if (p.download) want.push_back(&p);
    if (want.empty()) return;
    std::sort(want.begin(), want.end(), [](const Peer* a, const Peer* b) { return a->waiting_since < b->waiting_since; });
    double& budget = download_budget_;
    budget = std::min(budget + std::max(0.0, now - download_last_) * double(options_.upload_kbps) * 128.0, double(options_.upload_kbps) * 128.0);
    download_last_ = now;
    for (size_t i = 0; i < want.size(); ++i) {
        Peer& p = *want[i];
        if (i >= options_.max_downloaders) {
            // Waiting: told their place once a second or so.
            if (now - p.download->last_chunk > 1.0) {
                ContentChunk c;
                c.sha256 = p.download->sha256;
                c.queue = u16(std::min<size_t>(i - options_.max_downloaders + 1, 0xFFFF));
                send_msg(p.id, c);
                p.download->last_chunk = now;
            }
            continue;
        }
        const ServerPack* pack = content_.pack(p.download->sha256);
        if (!pack) {
            p.download.reset();
            continue;
        }
        const std::vector<u8>* bytes = &pack->bytes;
        if (options_.test_evil & kEvilStall) continue;   // a hostile server (tests): nothing ever comes
        for (int k = 0; k < 4 && budget > 0 && p.download->offset < pack->size && net_.pending_reliable(p.id) < 8; ++k) {
            ContentChunk c;
            c.sha256 = pack->sha256;
            c.total = pack->size;
            c.offset = p.download->offset;
            const size_t n = std::min<size_t>(kContentChunk, pack->size - p.download->offset);
            c.bytes.assign(bytes->begin() + std::ptrdiff_t(p.download->offset), bytes->begin() + std::ptrdiff_t(p.download->offset + n));
            if ((options_.test_evil & kEvilCorruptChunks) && !c.bytes.empty()) c.bytes[0] ^= 0xFF;
            p.download->offset += u32(n);
            p.download->last_chunk = now;
            budget -= double(n);
            send_msg(p.id, c);
        }
        if (p.download->offset >= pack->size) p.download.reset();
    }
}

}  // namespace lsfs
