
#include "util/pch.h"
#include "plugin_system/plugin_manager.h"
#include "application.h"

// FORWARD DECLARATIONS ================================================================================================

// CONSTANTS =======================================================================================================

// MACROS ==========================================================================================================

#if defined(PLATFORM_LINUX)
    #define ARGC        argc
    #define ARGV        argv
    #define MAIN_FUNC   main(int argc, char* argv[])
#elif defined(PLATFORM_WINDOWS)
    #include <Windows.h>
    #define ARGC        __argc
    #define ARGV        __argv
    #define MAIN_FUNC   WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif

// TYPES ===========================================================================================================

#include "util/util.h"
#include "util/io/serializer_yaml.h"


enum class endian {

    little = 0,
    big,
};


enum class field_type {

    byte_1 = 0,
    byte_2,
    byte_3,
    byte_4,
    CRC_8,
    CRC_16,
    CRC_32,
    CRC_64,
    frame,
};


struct field {

    std::string                 name{};
    std::string                 description{};
    field_type                  type = field_type::byte_1;
    std::string                 frame_name{};
};


struct frame_data {

    u8                          start_byte;
    std::vector<field>          fields{};
};


enum class crc_algorithm {

    // CRC-8 variants
    CRC8 = 0,
    CRC8_MAXIM,
    CRC8_SMBUS,

    // CRC-16 variants
    XMODEM,
    CCITT,
    CCITT_FALSE,
    MODBUS,
    IBM,          // same as ARC
    MAXIM,
    USB,
    DNP,

    // CRC-32 variants
    CRC32,        // standard Ethernet/ZIP / ISO-HDLC
    CRC32_MPEG2,
    CRC32C,       // Castagnoli
    CRC32_BZIP2,
    CRC32_JAMCRC,

    // CRC-64 variants
    CRC64_ISO,
    CRC64_ECMA
};


struct crc_data {

    crc_algorithm               algorithm;       // e.g. "XMODEM"
    u32                         polynomial;      // could be 16-bit, but uint32_t covers all sizes
    u16                         initial_value;
    u16                         final_xor;
    bool                        reflect_in;
    bool                        reflect_out;
    std::vector<std::string>    over_fields;     // names of the fields to compute CRC over
    endian                      byte_order;      // how the CRC value is stored (little/big)
};


struct protocol {
    
    std::string                 name{};
    std::string                 description{};
    GLT::version                 version{};
    endian                      default_endian = endian::little;

    frame_data                  physical_frame{};
};

// STATIC VARIABLES ================================================================================================

// FUNCTION IMPLEMENTATION =========================================================================================


int MAIN_FUNC {
    
    {
        GLT::plugin_manager::discover_plugins();
        GLT::plugin_manager::load_plugins(GLT::plugin_manager::load_phase::earliest_possible);
        GLT::config::init();
        GLT::plugin_manager::load_plugins(GLT::plugin_manager::load_phase::post_config_init);
        GLT::logger::register_label_for_thread("main");
        GLT::logger::init("[$B$T:$J$E] [$B$R $L$X $Q - $I:$P:$G$E] $C$Z", true, GLT::util::get_executable_path() / "logs", "gluttony.log", true);
        GLT::logger::set_buffer_threshold(GLT::logger::severity::warn);
        GLT::crash_handler::attach();
        GLT::crash_handler::subscribe(GLT::logger::shutdown);
        GLT::plugin_manager::load_plugins(GLT::plugin_manager::load_phase::post_setup);
    }





    protocol prot{};

    bool success = false;
    GLT::serializer::yaml("./protocol/PCP_def.yml", "protocol", GLT::serializer::option::load, &success)
        .entry(KEY_VALUE(prot.name))
        .entry(KEY_VALUE(prot.description))
        .entry(KEY_VALUE(prot.version))
        .entry(KEY_VALUE(prot.default_endian))
        
        .sub_section("physical_frame", [&](GLT::serializer::yaml& y_field) {
            y_field.entry(KEY_VALUE(prot.physical_frame.start_byte))
                .vector(KEY_VALUE(prot.physical_frame.fields), [&](GLT::serializer::yaml& y_field, const u64 x) {
                    y_field.entry(KEY_VALUE(prot.physical_frame.fields[x].name))
                        .entry(KEY_VALUE(prot.physical_frame.fields[x].description))
                        .entry(KEY_VALUE(prot.physical_frame.fields[x].type));

                        switch (prot.physical_frame.fields[x].type)
                        {
                            case field_type::byte_1:    break;
                            case field_type::byte_2:    break;
                            case field_type::byte_3:    break;
                            case field_type::byte_4:    break;
                            case field_type::CRC_8:     break;

                            case field_type::CRC_16:    { 

                                y_field.sub_section("crc", [&](GLT::serializer::yaml& y_crc) {

                                });

                            } break;

                            case field_type::CRC_32:    break;
                            case field_type::CRC_64:    break;
                            case field_type::frame:     {

                                // if [type] is [field_type::frame] -> then [frame_name] holds the name of the frame it points to
                                std::string frame_name{};
                                y_field.entry(KEY_VALUE(frame_name));
                                
                            } break;

                            default:                    break;
                        }
                });
        });

    ASSERT(success, "", "Failed to serialize protocol");
    LOG(trace, "Protocol:");
    LOG(trace, "  name           : {}", prot.name);
    LOG(trace, "  description    : {}", prot.description);
    LOG(trace, "  version        : {}", GLT::util::to_string(prot.version));                // assumes GLT::version is formattable
    LOG(trace, "  default_endian : {}", GLT::util::enum_to_string(prot.default_endian));

    const auto& frame = prot.physical_frame;
    LOG(trace, "  physical_frame:");
    LOG(trace, "    start_byte   : 0x{:02X}", frame.start_byte);
    LOG(trace, "    fields       : {}", frame.fields.size());

    for (size_t i = 0; i < frame.fields.size(); ++i) {
        const auto& f = frame.fields[i];
        LOG(trace, "    field[{}]:", i);
        LOG(trace, "      name        : {}", f.name);
        LOG(trace, "      description : {}", f.description);
        LOG(trace, "      type        : {}", GLT::util::enum_to_string(f.type));
        if (!f.frame_name.empty()) {
            LOG(trace, "      frame_name  : {}", f.frame_name);
        }
    }









    // {
    //     GLT::application app{ARGC, ARGV};
    //     app.run();
    // }

    {
        GLT::plugin_manager::load_plugins(GLT::plugin_manager::load_phase::final_cleanup);
        GLT::logger::shutdown();
        GLT::crash_handler::detach();
        GLT::plugin_manager::shutdown();
    }

    return EXIT_SUCCESS;
}

#undef MAIN_FUNC

// CLASS IMPLEMENTATION ============================================================================================

// CLASS PUBLIC ====================================================================================================

// CLASS PROTECTED =================================================================================================

// CLASS PRIVATE ===================================================================================================
