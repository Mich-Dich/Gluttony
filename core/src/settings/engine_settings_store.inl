#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::engine_settings {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    namespace detail {

        using GLT::reflect::member_descriptor;
        using GLT::reflect::type_kind;

        [[nodiscard]] inline size_t value_size(type_kind kind);

        // Member value <-> number. Every supported type fits a double exactly except 64 bit integers beyond 2^53, which
        // no setting needs
        [[nodiscard]] inline f64 to_number(type_kind kind, const void* value);
        inline void from_number(type_kind kind, f64 number, void* value);

        // Brings a value into the member's range (and its type's range)
        [[nodiscard]] inline f64 limit(const member_descriptor& member, f64 number);

        [[nodiscard]] inline std::string to_text(const member_descriptor& member, const void* value);
        [[nodiscard]] inline bool from_text(const member_descriptor& member, std::string_view text, void* value);

        [[nodiscard]] inline std::string_view trim(std::string_view text);
    }

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace detail {

        inline size_t value_size(const type_kind kind) {

            switch (kind) {
                case type_kind::boolean:                        return sizeof(bool);
                case type_kind::u8:  case type_kind::i8:        return 1;
                case type_kind::u16: case type_kind::i16:       return 2;
                case type_kind::u32: case type_kind::i32:       return 4;
                case type_kind::u64: case type_kind::i64:       return 8;
                case type_kind::f32:                            return sizeof(f32);
                case type_kind::f64:                            return sizeof(f64);
                default:                                        return 0;
            }
        }


        inline f64 to_number(const type_kind kind, const void* value) {

            #define GLT_READ_AS(type)   { type v{}; std::memcpy(&v, value, sizeof(v)); return static_cast<f64>(v); }
            switch (kind) {
                case type_kind::boolean:    GLT_READ_AS(bool)
                case type_kind::u8:         GLT_READ_AS(u8)
                case type_kind::u16:        GLT_READ_AS(u16)
                case type_kind::u32:        GLT_READ_AS(u32)
                case type_kind::u64:        GLT_READ_AS(u64)
                case type_kind::i8:         GLT_READ_AS(i8)
                case type_kind::i16:        GLT_READ_AS(i16)
                case type_kind::i32:        GLT_READ_AS(i32)
                case type_kind::i64:        GLT_READ_AS(i64)
                case type_kind::f32:        GLT_READ_AS(f32)
                case type_kind::f64:        GLT_READ_AS(f64)
                default:                    return 0.0;
            }
            #undef GLT_READ_AS
        }


        inline void from_number(const type_kind kind, const f64 number, void* value) {

            #define GLT_WRITE_AS(type)  { const type v = static_cast<type>(number); std::memcpy(value, &v, sizeof(v)); return; }
            switch (kind) {
                case type_kind::boolean:    { const bool v = number != 0.0; std::memcpy(value, &v, sizeof(v)); return; }
                case type_kind::u8:         GLT_WRITE_AS(u8)
                case type_kind::u16:        GLT_WRITE_AS(u16)
                case type_kind::u32:        GLT_WRITE_AS(u32)
                case type_kind::u64:        GLT_WRITE_AS(u64)
                case type_kind::i8:         GLT_WRITE_AS(i8)
                case type_kind::i16:        GLT_WRITE_AS(i16)
                case type_kind::i32:        GLT_WRITE_AS(i32)
                case type_kind::i64:        GLT_WRITE_AS(i64)
                case type_kind::f32:        GLT_WRITE_AS(f32)
                case type_kind::f64:        GLT_WRITE_AS(f64)
                default:                    return;
            }
            #undef GLT_WRITE_AS
        }


        inline f64 limit(const member_descriptor& member, f64 number) {

            if (std::isnan(number))
                number = 0.0;

            if ((member.flags & static_cast<u32>(GLT::reflect::member_flags::has_range)) != 0u && member.range_min <= member.range_max)
                number = std::clamp(number, member.range_min, member.range_max);

            const auto whole = [&](const f64 low, const f64 high) { number = std::clamp(std::round(number), low, high); };
            switch (member.kind) {
                case type_kind::boolean:    number = number != 0.0 ? 1.0 : 0.0;                 break;
                case type_kind::u8:         whole(0.0, 255.0);                                  break;
                case type_kind::u16:        whole(0.0, 65535.0);                                break;
                case type_kind::u32:        whole(0.0, 4294967295.0);                           break;
                case type_kind::u64:        whole(0.0, 9007199254740992.0);                     break;
                case type_kind::i8:         whole(-128.0, 127.0);                               break;
                case type_kind::i16:        whole(-32768.0, 32767.0);                           break;
                case type_kind::i32:        whole(-2147483648.0, 2147483647.0);                 break;
                case type_kind::i64:        whole(-9007199254740992.0, 9007199254740992.0);     break;
                case type_kind::f32:        number = std::clamp(number, static_cast<f64>(std::numeric_limits<f32>::lowest()),
                                                static_cast<f64>(std::numeric_limits<f32>::max()));
                                            break;
                default:                    break;
            }
            return number;
        }


        inline std::string to_text(const member_descriptor& member, const void* value) {

            switch (member.kind) {
                case type_kind::boolean:    return to_number(member.kind, value) != 0.0 ? "true" : "false";
                case type_kind::f32:        { f32 v{}; std::memcpy(&v, value, sizeof(v)); return std::format("{}", v); }    // shortest text that reads back the same
                case type_kind::f64:        { f64 v{}; std::memcpy(&v, value, sizeof(v)); return std::format("{}", v); }
                default:                    return std::format("{:.0f}", to_number(member.kind, value));
            }
        }


        inline bool from_text(const member_descriptor& member, std::string_view text, void* value) {

            text = trim(text);
            f64 number = 0.0;

            if (member.kind == type_kind::boolean && (text == "true" || text == "false"))
                number = text == "true" ? 1.0 : 0.0;
            else {
                const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
                if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
                    return false;
            }

            from_number(member.kind, limit(member, number), value);
            return true;
        }


        inline std::string_view trim(std::string_view text) {

            const auto is_space = [](const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
            while (!text.empty() && is_space(text.front()))
                text.remove_prefix(1);
            while (!text.empty() && is_space(text.back()))
                text.remove_suffix(1);
            return text;
        }
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    inline bool is_setting(const GLT::reflect::member_descriptor& member) {

        using flags = GLT::reflect::member_flags;
        constexpr u32 hidden = static_cast<u32>(flags::skip) | static_cast<u32>(flags::transient) | static_cast<u32>(flags::readonly);
        return (member.flags & hidden) == 0u && member.name && member.name[0] && detail::value_size(member.kind) != 0;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    inline bool store::add(block_desc desc) {

        if (desc.name.empty() || !desc.type || !desc.live || desc.type->size == 0 || find(desc.name))
            return false;

        block created{};
        created.defaults.resize(desc.type->size);
        std::memcpy(created.defaults.data(), desc.live, desc.type->size);       // settings structs are plain values
        created.desc = std::move(desc);
        m_blocks.push_back(std::move(created));
        apply(m_blocks.back(), false);
        return true;
    }


    inline void store::remove(const std::string_view name) {

        std::erase_if(m_blocks, [&](const block& item) { return item.desc.name == name; });
    }


    inline const store::block* store::find(const std::string_view name) const {

        for (const block& item : m_blocks)
            if (item.desc.name == name)
                return &item;
        return nullptr;
    }


    inline void store::read(const block& owner, const GLT::reflect::member_descriptor& member, const scope view, void* value) const {

        resolve(owner, member, view, value);
    }


    inline void store::read_inherited(const block& owner, const GLT::reflect::member_descriptor& member, const scope target, void* value) const {

        if (target == scope::user)
            resolve(owner, member, scope::project, value);
        else
            std::memcpy(value, owner.defaults.data() + member.offset, detail::value_size(member.kind));
    }


    inline void store::write(const block& owner, const GLT::reflect::member_descriptor& member, const scope target, const void* value) {

        if (!is_setting(member))
            return;

        // Through the same limits as a value read from a file
        std::array<std::byte, 8> wanted{};
        detail::from_number(member.kind, detail::limit(member, detail::to_number(member.kind, value)), wanted.data());

        std::array<std::byte, 8> inherited{};
        read_inherited(owner, member, target, inherited.data());

        value_map& values = m_layers[static_cast<size_t>(target)][owner.desc.name];
        const size_t size = detail::value_size(member.kind);
        if (std::memcmp(wanted.data(), inherited.data(), size) == 0) {
            if (values.erase(member.name) != 0)
                m_dirty[static_cast<size_t>(target)] = true;
        } else {
            std::string text = detail::to_text(member, wanted.data());
            auto it = values.find(member.name);
            if (it == values.end() || it->second != text) {
                values.insert_or_assign(member.name, std::move(text));
                m_dirty[static_cast<size_t>(target)] = true;
            }
        }
        if (values.empty())
            m_layers[static_cast<size_t>(target)].erase(owner.desc.name);

        apply(owner, true);
    }


    inline void store::clear(const block& owner, const GLT::reflect::member_descriptor& member, const scope target) {

        block_map& blocks = m_layers[static_cast<size_t>(target)];
        auto it = blocks.find(owner.desc.name);
        if (it == blocks.end())
            return;
        if (it->second.erase(member.name) != 0)
            m_dirty[static_cast<size_t>(target)] = true;
        if (it->second.empty())
            blocks.erase(it);
        apply(owner, true);
    }


    inline bool store::is_set(const block& owner, const GLT::reflect::member_descriptor& member, const scope target) const {

        return stored(owner, member, target) != nullptr;
    }


    inline bool store::adopt_live(const block& owner, const scope target) {

        // write() puts the stored values back into the live instance, so work on a copy of what was edited
        std::vector<std::byte> edited(owner.desc.type->size);
        std::memcpy(edited.data(), owner.desc.live, edited.size());

        bool adopted = false;
        for (const auto& member : owner.desc.type->members) {

            if (!is_setting(member))
                continue;

            std::array<std::byte, 8> current{};
            resolve(owner, member, scope::user, current.data());
            const std::byte* live = edited.data() + member.offset;
            if (std::memcmp(current.data(), live, detail::value_size(member.kind)) == 0)
                continue;

            std::array<std::byte, 8> wanted{};
            std::memcpy(wanted.data(), live, detail::value_size(member.kind));
            write(owner, member, target, wanted.data());
            adopted = true;
        }
        return adopted;
    }


    inline void store::parse(const scope target, const std::string_view text) {

        block_map parsed{};
        value_map* current = nullptr;

        size_t position = 0;
        while (position <= text.size()) {

            const size_t line_end = std::min(text.find('\n', position), text.size());
            std::string_view line = text.substr(position, line_end - position);
            position = line_end + 1;

            if (const size_t comment = line.find('#'); comment != std::string_view::npos)
                line = line.substr(0, comment);
            const bool indented = !line.empty() && (line.front() == ' ' || line.front() == '\t');
            line = detail::trim(line);
            if (line.empty())
                continue;

            const size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            const std::string_view key = detail::trim(line.substr(0, colon));
            const std::string_view value = detail::trim(line.substr(colon + 1));
            if (key.empty())
                continue;

            if (!indented) {                                // "block name:" starts a block
                current = value.empty() ? &parsed[std::string(key)] : nullptr;
                continue;
            }
            if (current && !value.empty())
                current->insert_or_assign(std::string(key), std::string(value));
        }

        std::erase_if(parsed, [](const auto& item) { return item.second.empty(); });
        m_layers[static_cast<size_t>(target)] = std::move(parsed);
        m_dirty[static_cast<size_t>(target)] = false;

        for (const block& item : m_blocks)
            apply(item, true);
    }


    inline std::string store::emit(const scope target) const {

        std::string out = target == scope::project
            ? "# Engine settings of this project. Only values that differ from the engine's defaults are listed.\n"
              "# Edit them in the editor: Edit > Engine Settings (Project).\n"
            : "# Engine settings changed on this computer, on top of the project's (engine.config).\n"
              "# Not meant to be shared: keep this file out of version control.\n";

        for (const auto& [block_name, values] : m_layers[static_cast<size_t>(target)]) {
            if (values.empty())
                continue;
            out += "\n";
            out += block_name;
            out += ":\n";
            for (const auto& [key, value] : values) {
                out += "  ";
                out += key;
                out += ": ";
                out += value;
                out += "\n";
            }
        }
        return out;
    }


    inline void store::reset() {

        for (auto& values : m_layers)
            values.clear();
        m_dirty.fill(false);
        for (const block& item : m_blocks)
            apply(item, true);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    inline const std::string* store::stored(const block& owner, const GLT::reflect::member_descriptor& member, const scope target) const {

        const block_map& blocks = m_layers[static_cast<size_t>(target)];
        const auto block_it = blocks.find(owner.desc.name);
        if (block_it == blocks.end())
            return nullptr;
        const auto value_it = block_it->second.find(std::string_view(member.name));
        return value_it != block_it->second.end() ? &value_it->second : nullptr;
    }


    inline void store::resolve(const block& owner, const GLT::reflect::member_descriptor& member, const scope view, void* value) const {

        std::memcpy(value, owner.defaults.data() + member.offset, detail::value_size(member.kind));

        // Lower layer first; text that does not parse leaves the value below it in place
        if (const std::string* text = stored(owner, member, scope::project))
            (void)detail::from_text(member, *text, value);
        if (view == scope::user)
            if (const std::string* text = stored(owner, member, scope::user))
                (void)detail::from_text(member, *text, value);
    }


    inline bool store::apply(const block& owner, const bool notify) {

        bool changed = false;
        for (const auto& member : owner.desc.type->members) {

            if (!is_setting(member))
                continue;

            std::array<std::byte, 8> value{};
            resolve(owner, member, scope::user, value.data());
            std::byte* live = static_cast<std::byte*>(owner.desc.live) + member.offset;
            const size_t size = detail::value_size(member.kind);
            if (std::memcmp(live, value.data(), size) != 0) {
                std::memcpy(live, value.data(), size);
                changed = true;
            }
        }

        if (changed && notify && owner.desc.on_changed)
            owner.desc.on_changed();
        return changed;
    }

}
