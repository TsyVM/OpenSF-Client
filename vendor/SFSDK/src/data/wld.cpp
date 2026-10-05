// SPDX-License-Identifier: MIT
#include "sf1/data/wld.hpp"
#include "binary_reader.hpp"

namespace sf1::data::wld {

Result<std::vector<Light>> read_lights(std::span<const std::byte> environment, std::uint64_t base_offset) noexcept {
    if (environment.size() != kEnvironmentSize) return err(Error::Malformed);
    std::uint32_t count = 0;
    std::memcpy(&count, environment.data(), 4);
    if (count > kLightSlots) return err(Error::CountOutOfRange);

    std::vector<Light> lights;
    lights.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = 4 + i * kLightSize;
        std::uint32_t type = 0;
        float f[25]{};
        std::memcpy(&type, environment.data() + at, 4);
        std::memcpy(f, environment.data() + at + 4, sizeof f);
        if (type < 1 || type > 3) return err(Error::Malformed);
        if (!detail::all_finite(f, 25)) return err(Error::NonFinite);

        Light l;
        l.offset = base_offset + at;
        l.type = static_cast<LightType>(type);
        std::memcpy(l.diffuse.data(), f + 0, 16);
        std::memcpy(l.specular.data(), f + 4, 16);
        std::memcpy(l.ambient.data(), f + 8, 16);
        std::memcpy(l.position.data(), f + 12, 12);
        std::memcpy(l.direction.data(), f + 15, 12);
        l.range = f[18];
        l.falloff = f[19];
        std::memcpy(l.attenuation.data(), f + 20, 12);
        l.theta = f[23];
        l.phi = f[24];
        lights.push_back(l);
    }
    return lights;
}

namespace {

Result<std::vector<std::string>> read_names(detail::BinaryReader& r, std::uint32_t cap) noexcept {
    auto count = r.count(cap);
    if (!count) return err(count.error());
    if (!r.can(std::size_t{*count} * kNameWidth)) return err(Error::ShortRead);
    std::vector<std::string> out;
    out.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        auto n = r.name(kNameWidth);
        if (!n) return err(n.error());
        out.push_back(std::move(*n));
    }
    return out;
}

Result<std::vector<Record>> read_records(detail::BinaryReader& r, std::size_t width) noexcept {
    auto count = r.count(100000);
    if (!count) return err(count.error());
    if (!r.can(std::size_t{*count} * width)) return err(Error::ShortRead);
    std::vector<Record> out(*count);
    for (auto& rec : out) {
        rec.offset = r.pos();
        auto raw = r.take(width);
        if (!raw) return err(raw.error());
        rec.raw.assign(raw->begin(), raw->end());
    }
    return out;
}

}  // namespace

Result<World> read(std::span<const std::byte> bytes) noexcept {
    detail::BinaryReader r(bytes);
    World w;

    auto version = r.u8();
    if (!version) return err(version.error());
    if (*version != kVersion) return err(Error::UnsupportedVersion);
    w.version = *version;

    auto mapping = r.name(kNameWidth);
    if (!mapping) return err(mapping.error());
    w.mapping = std::move(*mapping);

    auto object_names = read_names(r, 100000);
    if (!object_names) return err(object_names.error());
    w.object_names = std::move(*object_names);

    auto sector_count = r.count(10000);
    if (!sector_count) return err(sector_count.error());
    w.sectors.resize(*sector_count);
    for (auto& s : w.sectors) {
        s.offset = r.pos();
        auto name = r.name(kSectorNameWidth);
        if (!name) return err(name.error());
        s.name = std::move(*name);

        auto objects = read_records(r, kObjectRecordSize);
        if (!objects) return err(objects.error());
        s.objects = std::move(*objects);
        auto secondary = read_records(r, kSecondaryRecordSize);
        if (!secondary) return err(secondary.error());
        s.secondary_objects = std::move(*secondary);

        for (std::size_t g = 0; g < 3; ++g) {
            auto count = r.u32();
            if (!count) return err(count.error());
            auto& group = s.groups[g];
            if (g == 1 && *count == 0xFFFFFFFF) {
                group.present = false;
                continue;
            }
            if (*count > 1000000) return err(Error::CountOutOfRange);
            group.count = *count;
            group.offset = r.pos();
            for (std::uint32_t i = 0; i < *count; ++i) {
                auto a = r.count(1000000);
                if (!a) return err(a.error());
                if (auto skip = r.take_array(*a, 4); !skip) return err(skip.error());
                if (g < 2) {
                    auto b = r.count(1000000);
                    if (!b) return err(b.error());
                    if (auto skip = r.take_array(*b, 4); !skip) return err(skip.error());
                }
            }
            group.size = r.pos() - group.offset;
        }
        s.end = r.pos();
    }

    auto object_paths = read_names(r, 100000);
    if (!object_paths) return err(object_paths.error());
    w.object_paths = std::move(*object_paths);

    auto name_len = r.count(4096);
    if (!name_len) return err(name_len.error());
    auto name = r.take(*name_len);
    if (!name) return err(name.error());
    std::size_t len = name->size();
    while (len > 0 && (*name)[len - 1] == std::byte{0}) --len;
    w.name.assign(reinterpret_cast<const char*>(name->data()), len);

    auto sky_enabled = r.u32();
    if (!sky_enabled) return err(sky_enabled.error());
    w.sky_enabled = *sky_enabled;
    w.sky_offset = r.pos();
    if (w.sky_enabled != 0) {
        for (int i = 0; i < 6; ++i) {
            auto sky = r.name(kNameWidth);
            if (!sky) return err(sky.error());
            w.sky.push_back(std::move(*sky));
        }
    }

    for (auto& s : w.sectors) {
        s.environment_offset = r.pos();
        auto env = r.take(kEnvironmentSize);
        if (!env) return err(env.error());
        s.environment.assign(env->begin(), env->end());
        auto lights = read_lights(*env, s.environment_offset);
        if (!lights) return err(lights.error());
        s.lights = std::move(*lights);
        auto extra = r.fixed<16>();
        if (!extra) return err(extra.error());
        s.environment_extra = *extra;
    }

    if (!r.at_end()) return err(Error::TrailingBytes);
    w.size = r.pos();
    return w;
}

Result<ObjectRecord> decode_object(const Record& record) noexcept {
    if (record.raw.size() != kObjectRecordSize) return err(Error::Malformed);
    const auto* raw = record.raw.data();
    ObjectRecord o;
    o.offset = record.offset;
    std::memcpy(o.position.data(), raw + 0, 12);
    std::memcpy(o.scale.data(), raw + 12, 12);
    std::memcpy(o.euler.data(), raw + 24, 12);
    std::memcpy(o.world.data(), raw + 36, 64);
    std::memcpy(o.bounds_max.data(), raw + 100, 12);
    std::memcpy(o.bounds_min.data(), raw + 112, 12);
    std::memcpy(&o.name_index, raw + 124, 4);
    if (!detail::all_finite(o.position.data(), 3) || !detail::all_finite(o.world.data(), 16) ||
        !detail::all_finite(o.bounds_min.data(), 3) || !detail::all_finite(o.bounds_max.data(), 3))
        return err(Error::NonFinite);
    return o;
}

bool write_sky(std::vector<std::byte>& bytes, const World& world, const std::vector<std::string>& faces) noexcept {
    if (world.sky_enabled == 0 || world.sky.size() != 6 || faces.size() != 6) return false;
    if (world.sky_offset + 6 * kNameWidth > bytes.size()) return false;
    for (const auto& face : faces)
        if (face.size() >= kNameWidth) return false;
    for (std::size_t i = 0; i < 6; ++i) {
        const auto at = static_cast<std::size_t>(world.sky_offset + i * kNameWidth);
        std::memcpy(bytes.data() + at, faces[i].data(), faces[i].size());
        bytes[at + faces[i].size()] = std::byte{0};
    }
    return true;
}

bool write_light(std::vector<std::byte>& bytes, const Light& light) noexcept {
    if (light.offset + kLightSize > bytes.size()) return false;
    float f[25]{};
    std::memcpy(f + 0, light.diffuse.data(), 16);
    std::memcpy(f + 4, light.specular.data(), 16);
    std::memcpy(f + 8, light.ambient.data(), 16);
    std::memcpy(f + 12, light.position.data(), 12);
    std::memcpy(f + 15, light.direction.data(), 12);
    f[18] = light.range;
    f[19] = light.falloff;
    std::memcpy(f + 20, light.attenuation.data(), 12);
    f[23] = light.theta;
    f[24] = light.phi;
    if (!detail::all_finite(f, 25)) return false;
    const auto type = static_cast<std::uint32_t>(light.type);
    const auto at = static_cast<std::size_t>(light.offset);
    std::memcpy(bytes.data() + at, &type, 4);
    std::memcpy(bytes.data() + at + 4, f, sizeof f);
    return true;
}

}  // namespace sf1::data::wld
