
#include "util/pch.h"
#include "entity_codec.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    template<typename T>
    void write_pod(std::vector<std::byte>& out, const T& v);

    template<typename T>
    T read_pod(std::span<const std::byte> data, size_t& off);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template<typename T>
    void write_pod(std::vector<std::byte>& out, const T& v) {

        const auto* p = reinterpret_cast<const std::byte*>(&v);
        out.insert(out.end(), p, p + sizeof(T));
    }

    template<typename T>
    T read_pod(std::span<const std::byte> data, size_t& off) {

        T v;
        std::memcpy(&v, data.data() + off, sizeof(T));
        off += sizeof(T);
        return v;
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================
    const entity_codec::entry* entity_codec::find(u64 hash) const noexcept {

        for (const auto& e : m_components)
            if (e.type_hash == hash)
                return &e;
        return nullptr;
    }


    void entity_codec::save(const entt::registry& reg, std::span<const entt::entity> entities, const resolve_fn& resolve, 
        std::vector<std::byte>& out) const {

        out.clear();
        write_pod<u32>(out, CODEC_ENTT_V1);
        write_pod<u32>(out, static_cast<u32>(entities.size()));

        for (auto ent : entities) {

            const GLT::world::entity_id id = resolve(ent);
            write_pod<u32>(out, id.index);
            write_pod<u32>(out, id.generation);

            // Count components first so the reader can skip cleanly.
            u32 count = 0;
            for (const auto& e : m_components)
                if (e.has(reg, ent))
                    ++count;
            write_pod<u32>(out, count);

            for (const auto& e : m_components) {
                if (!e.has(reg, ent))
                    continue;

                write_pod<u64>(out, e.type_hash);

                // Reserve size, write payload, backfill size.
                const size_t size_pos = out.size();
                write_pod<u32>(out, 0);
                const size_t data_start = out.size();
                e.save(reg, ent, out);
                const u32 actual = static_cast<u32>(out.size() - data_start);
                std::memcpy(out.data() + size_pos, &actual, sizeof(u32));
            }
        }
    }


    std::vector<GLT::world::entity_id> entity_codec::load(entt::registry& reg, std::span<const std::byte> data, const allocate_fn& allocate) const {

        std::vector<GLT::world::entity_id> created;
        if (data.size() < 8)
            return created;

        size_t off = 0;
        const u32 version = read_pod<u32>(data, off);
        if (version != CODEC_ENTT_V1)
            return created;

        const u32 entity_count = read_pod<u32>(data, off);
        created.reserve(entity_count);

        for (u32 i = 0; i < entity_count; ++i) {

            GLT::world::entity_id saved;
            saved.index = read_pod<u32>(data, off);
            saved.generation = read_pod<u32>(data, off);

            auto [new_id, ent] = allocate(saved);
            created.push_back(new_id);

            const u32 comp_count = read_pod<u32>(data, off);
            for (u32 c = 0; c < comp_count; ++c) {

                const u64 hash = read_pod<u64>(data, off);
                const u32 size = read_pod<u32>(data, off);
                const auto sub = data.subspan(off, size);
                off += size;

                if (const entry* e = find(hash))
                    e->load(reg, ent, sub);
                // else: unknown component - skip silently (forward compat).
            }
        }

        return created;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
