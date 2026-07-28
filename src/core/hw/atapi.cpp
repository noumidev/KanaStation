/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/atapi.cpp - ATAPI for UMD */

#include <core/hw/atapi.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/gpio.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::atapi {

using namespace common;

constexpr u64 ATAPI_AHB_ADDR = 0x1D600000;
constexpr u64 ATAPI_ADDR     = 0x1D700000;
constexpr u64 ATAPI_SIZE     = 0x1000;

constexpr int ATA_INTERRUPT = 5;

constexpr u32 AHB_REVISION = 0x00010033;

// https://www.psdevwiki.com/psp/Hardware_Registers#0xBD600000:_ATA/UMD

enum IoAddress {
    IO_ADDRESS_AHB_REVISION = ATAPI_AHB_ADDR + 0x000,
    IO_ADDRESS_AHB_CONTROL  = ATAPI_AHB_ADDR + 0x004,
    IO_ADDRESS_AHB_RESET    = ATAPI_AHB_ADDR + 0x010,
    IO_ADDRESS_AHB_PIODLY   = ATAPI_AHB_ADDR + 0x014,
    IO_ADDRESS_AHB_MDMADLY  = ATAPI_AHB_ADDR + 0x01C,
    IO_ADDRESS_AHB_PIOCTRL  = ATAPI_AHB_ADDR + 0x034,
    IO_ADDRESS_AHB_INTR     = ATAPI_AHB_ADDR + 0x044,
    IO_ADDRESS_AHB_DEVSTAT  = ATAPI_AHB_ADDR + 0x04C,
    IO_ADDRESS_DATA         = ATAPI_ADDR     + 0x000,
    IO_ADDRESS_FEATURES     = ATAPI_ADDR     + 0x001,
    IO_ADDRESS_REASON       = ATAPI_ADDR     + 0x002,
    IO_ADDRESS_LBA_LO       = ATAPI_ADDR     + 0x003,
    IO_ADDRESS_LBA_MID      = ATAPI_ADDR     + 0x004,
    IO_ADDRESS_LBA_HI       = ATAPI_ADDR     + 0x005,
    IO_ADDRESS_DRIVE        = ATAPI_ADDR     + 0x006,
    IO_ADDRESS_COMMAND      = ATAPI_ADDR     + 0x007,
    IO_ADDRESS_STATUS       = ATAPI_ADDR     + 0x007,
    IO_ADDRESS_PKTEND       = ATAPI_ADDR     + 0x008,
    IO_ADDRESS_DEVCTRL      = ATAPI_ADDR     + 0x00E,
    IO_ADDRESS_ALTSTAT      = ATAPI_ADDR     + 0x00E,
};

enum AtaCommand {
    ATA_COMMAND_PACKET = 0xA0,
};

enum ScsiCommand {
    SCSI_COMMAND_TEST_UNIT_READY     = 0x00,
    SCSI_COMMAND_REQUEST_SENSE       = 0x03,
    SCSI_COMMAND_INQUIRY             = 0x12,
    SCSI_COMMAND_READ_LONG           = 0x28,
    SCSI_COMMAND_MODE_SELECT_LONG    = 0x55,
    SCSI_COMMAND_MODE_SENSE_LONG     = 0x5A,
    SCSI_COMMAND_READ_DISC_STRUCTURE = 0xAD,
};

enum SenseKey {
    SENSE_KEY_NO_SENSE  = 0x0,
    SENSE_KEY_NOT_READY = 0x2,
};

// This is an additional sense code + qualifier pair
enum AdditionalSense {
    ADDITIONAL_SENSE_NO_ADDITIONAL_SENSE = 0x0000,
    ADDITIONAL_SENSE_MEDIUM_NOT_PRESENT  = 0x3A00,
};

enum AtapiState {
    ATAPI_STATE_IDLE,
    ATAPI_STATE_AWAIT_PACKET,
    ATAPI_STATE_AWAIT_DATA,
};

#define HW_ATAPI_AHB_PIOCTRL ctx.ahb.pio_control
#define HW_ATAPI_AHB_INTR    ctx.ahb.interrupt
#define HW_ATAPI_AHB_DEVSTAT ctx.ahb.device_status
#define HW_ATAPI_FEATURES    ctx.features
#define HW_ATAPI_REASON      ctx.interrupt_reason
#define HW_ATAPI_LBA         ctx.lba.raw
#define HW_ATAPI_LBA_LO      ctx.lba.lo
#define HW_ATAPI_LBA_MID     ctx.lba.mid
#define HW_ATAPI_LBA_HI      ctx.lba.hi
#define HW_ATAPI_DRIVE       ctx.drive
#define HW_ATAPI_STATUS      ctx.status
#define HW_ATAPI_DEVCTRL     ctx.device_control

static struct {
    // I wonder if the AHB regs have any connection to SPOCK/LEPTON
    struct {
        u32 pio_control;
        u32 interrupt; // Combined mask/status?
        u32 device_status;
    } ahb;

    u8 features;

    union {
        u8 raw;

        struct {
            u8 is_command  : 1;
            u8 from_device : 1;
            u8             : 6;
        };
    } interrupt_reason;

    union {
        u32 raw;

        struct {
            u32 lo  : 8;
            u32 mid : 8;
            u32 hi  : 8;
            u32     : 8;
        };
    } lba;

    u8 drive;

    union {
        u8 raw;

        // Some bits are probably unused/different, but it should be fine
        struct {
            u8 check_condition : 1;
            u8 sense_available : 1;
            u8 alignment_error : 1;
            u8 data_request    : 1;
            u8 write_error     : 1;
            u8 device_fault    : 1;
            u8 device_ready    : 1;
            u8 busy            : 1;
        };
    } status;

    union {
        u8 raw;

        struct {
            u8                   : 1;
            u8 interrupt_disable : 1;
            u8 software_reset    : 1;
            u8                   : 5;
        };
    } device_control;

    SenseKey sense_key;
    AdditionalSense additional_sense;

    bool transfer_from_host;
} ctx;

struct ScsiRetval {
    // Apart from length, these should be the most common return values
    u16 length = 0;
    bool to_host = true;
    SenseKey sense_key = SenseKey::SENSE_KEY_NO_SENSE;
    AdditionalSense additional_sense = AdditionalSense::ADDITIONAL_SENSE_NO_ADDITIONAL_SENSE;
};

class Umd {
private:
    FILE* file;

    u64 file_size;

    u32 lba;
    u16 num_sectors;

public:
    static constexpr u64 SECTOR_SIZE = 2048;
    static constexpr u64 NUM_SECTORS_SINGLE = 460800;
    static constexpr u64 NUM_SECTORS_DUAL   = 2 * NUM_SECTORS_SINGLE;

    bool is_mounted() const {
        return file != nullptr;
    }

    bool mount(const char* path) {
        if (path == nullptr) {
            std::printf("NO PATH\n");
            return false;
        }

        file = std::fopen(path, "rb");

        if (file == nullptr) {
            std::printf("NO FILE\n");
            return false;
        }

        std::fseek(file, 0, SEEK_END);
        file_size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);

        return true;
    }

    u32 get_lba() const {
        return lba;
    }

    void set_lba(const int lba) {
        assert(lba >= 0);

        this->lba = lba;
    }

    u32 get_num_sectors() {
        return num_sectors;
    }

    void set_num_sectors(const u32 num_sectors) {
        this->num_sectors = num_sectors;
    }

    void seek() {
        assert((lba * SECTOR_SIZE) < file_size);

        std::fseek(file, lba * SECTOR_SIZE, SEEK_SET);

        lba++;
    }

    void read(u8* buf) {
        std::fread(buf, sizeof(u8), SECTOR_SIZE, file);
    }
};

static std::shared_ptr<spdlog::logger> logger;

static AtapiState state = AtapiState::ATAPI_STATE_IDLE;

static std::queue<u16> in_fifo;
static std::queue<u16> out_fifo;

static std::vector<u8> in_params;

static Umd umd;

static inline void state_transition(const AtapiState new_state) {
    state = new_state;
}

static inline void set_reason(const bool is_command, const bool from_device) {
    HW_ATAPI_REASON.is_command  = is_command;
    HW_ATAPI_REASON.from_device = from_device;

    ctx.transfer_from_host = !from_device;
}

static void assert_interrupt() {
    if (!HW_ATAPI_DEVCTRL.interrupt_disable) {
        intc::assert_sc_interrupt(ATA_INTERRUPT);
    }
}

static void check_condition(const SenseKey sense_key, const AdditionalSense additional_sense) {
    if (sense_key == SenseKey::SENSE_KEY_NO_SENSE) {
        HW_ATAPI_STATUS.check_condition = 0;
        HW_ATAPI_STATUS.sense_available = 0;
    } else {
        HW_ATAPI_STATUS.check_condition = 1;
        HW_ATAPI_STATUS.sense_available = 1;
    }

    ctx.sense_key = sense_key;
    ctx.additional_sense = additional_sense;
}

static inline u16 get_length() {
    return (HW_ATAPI_LBA_HI << 8) | HW_ATAPI_LBA_MID;
}

static void assert_scsi_interrupt(const ScsiRetval retval) {
    HW_ATAPI_LBA_MID = retval.length;
    HW_ATAPI_LBA_HI  = retval.length >> 8;

    HW_ATAPI_STATUS.data_request = retval.length != 0;
    HW_ATAPI_STATUS.device_ready = retval.length == 0;

    set_reason(retval.length == 0, retval.to_host);
    check_condition(retval.sense_key, retval.additional_sense);

    HW_ATAPI_STATUS.busy = 0;
    
    assert_interrupt();

    if (retval.length > 0) {
        state_transition(AtapiState::ATAPI_STATE_AWAIT_DATA);
    } else {
        state_transition(AtapiState::ATAPI_STATE_IDLE);
    }
}

static void get_in_params() {
    assert(!in_fifo.empty());

    in_params.clear();

    while (!in_fifo.empty()) {
        const u16 data = in_fifo.front(); in_fifo.pop();

        in_params.push_back(data >> 0);
        in_params.push_back(data >> 8);
    }
}

static u16 get_out_data() {
    assert(!out_fifo.empty());

    u16 data = out_fifo.front(); out_fifo.pop();

    if (!out_fifo.empty()) {
        data |= out_fifo.front() << 8; out_fifo.pop();
    }

    if (out_fifo.empty()) {
        assert_transfer_end_interrupt();
    }

    return data;
}

static void write_out_fifo(const u8 data) {
    out_fifo.push(data);
}

static void write_out_fifo(const char* data, const int length) {
    for (int i = 0; i < length; i++) {
        out_fifo.push(data[i]);
    }
}

static void clear_fifos() {
    while (!in_fifo.empty()) {
        in_fifo.pop();
    }

    while (!out_fifo.empty()) {
        out_fifo.pop();
    }
}

static ScsiRetval scsi_command_test_unit_ready() {
    logger->debug("SCSI TEST_UNIT_READY");

    if (!umd.is_mounted()) {
        check_condition(SenseKey::SENSE_KEY_NOT_READY, AdditionalSense::ADDITIONAL_SENSE_MEDIUM_NOT_PRESENT);
    } else {
        check_condition(SenseKey::SENSE_KEY_NO_SENSE, AdditionalSense::ADDITIONAL_SENSE_NO_ADDITIONAL_SENSE);
    }

    return {};
}

static ScsiRetval scsi_command_request_sense() {
    constexpr u8 REQUEST_SENSE_SIZE = 0x12;

    constexpr u8 VALID = 0x80;
    constexpr u8 RESPONSE_CODE = 0x70;

    logger->debug("SCSI REQUEST_SENSE");

    const u8 descriptor_format = in_params[1] & 1;

    // 0 means we return fixed-format sense data
    assert(descriptor_format == 0);

    write_out_fifo(VALID | RESPONSE_CODE);
    write_out_fifo(0);
    write_out_fifo(ctx.sense_key);
    // Information field. I don't know what values this can have
    write_out_fifo(0);
    write_out_fifo(0);
    write_out_fifo(0);
    write_out_fifo(0);
    write_out_fifo(REQUEST_SENSE_SIZE - 7);
    // Command-specific data. TEST_UNIT_READY might not return data here?
    write_out_fifo(0);
    write_out_fifo(0);
    write_out_fifo(0);
    write_out_fifo(0);
    // ASC(Q)
    write_out_fifo(ctx.additional_sense >> 8);
    write_out_fifo(ctx.additional_sense >> 0);
    // Field-replacable unit code. Probably always 0 if there is no
    // hardware failure
    write_out_fifo(0);
    // Sense key-specific data. The standard says this should always contain valid
    // info, but I don't know if UMD drives return anything here
    write_out_fifo(VALID);
    write_out_fifo(0);
    write_out_fifo(0);

    return {REQUEST_SENSE_SIZE};
}

static ScsiRetval scsi_command_inquiry() {
    constexpr u8 INQUIRY_SIZE = 0x60;

    // Some identifying info for UMD drives
    constexpr u8 MULTIMEDIA_DEVICE  = 0x05;
    constexpr u8 IS_REMOVABLE_MEDIA = 0x80;
    constexpr u8 VERSION = 0;
    constexpr u8 RESPONSE_FORMAT = 2;
    constexpr u8 HISUP   = 0x10;
    constexpr u8 NORMACA = 0x20;

    logger->debug("SCSI INQUIRY");

    write_out_fifo(MULTIMEDIA_DEVICE);
    write_out_fifo(IS_REMOVABLE_MEDIA);
    write_out_fifo(VERSION);
    write_out_fifo(NORMACA | HISUP | RESPONSE_FORMAT);
    write_out_fifo(INQUIRY_SIZE - sizeof(u32));
    write_out_fifo(0);
    write_out_fifo(0);
    write_out_fifo(0);

    // Unless I misunderstood the INQUIRY response format, the following
    // is a little nonstandard

    // T10 vendor ID
    write_out_fifo("SCEI    ", 8);
    // Product ID
    write_out_fifo("UMD ROM DRIVE   ", 16);
    // Product revision
    write_out_fifo("    ", 4);
    // Drive serial/vendor-unique
    write_out_fifo("1.150AAug30 ,2005   ", 20);

    return {(u16)out_fifo.size()};
}

static ScsiRetval scsi_command_read_long() {
    logger->debug("SCSI READ (10)");

    assert(umd.is_mounted());

    const u32 lba = (in_params[2] << 24) | (in_params[3] << 16) | (in_params[4] << 8) | in_params[5];
    const u16 num_sectors = (in_params[7] << 8) | in_params[8];

    umd.set_lba(lba);
    umd.set_num_sectors(num_sectors);

    return {1};
}

enum LogPage {
    LOG_PAGE_POWER_CONDITION_TRANSITIONS = 0x1A00,
};

// This command might have a 6-byte CDB variant
static ScsiRetval scsi_command_mode_select_long() {
    logger->debug("SCSI MODE_SELECT (10)");

    const u16 data_length = (in_params[7] << 8) | in_params[8];

    // Await data from the host
    return {data_length, false};
}

// This command might have a 6-byte CDB variant, too
static ScsiRetval scsi_command_mode_sense_long() {
    constexpr u16 MODE_SENSE_SIZE = 0x1C;

    logger->debug("SCSI MODE_SENSE (10)");

    const u16 log_page_code = (in_params[2] << 8) | in_params[3];

    switch (log_page_code) {
        case LogPage::LOG_PAGE_POWER_CONDITION_TRANSITIONS: {
            const u16 data_length = MODE_SENSE_SIZE - sizeof(u16);

            logger->trace("POWER_CONDITION_TRANSITIONS");

            // Until I can check what values my PSPs return, we will use JPCSP's
            write_out_fifo(data_length >> 8);
            write_out_fifo(data_length >> 0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0x9A);
            write_out_fifo(0x12);
            write_out_fifo(0);
            write_out_fifo(2);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(6);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(4);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(4);
            break;
        }
        default:
            logger->error("Unimplemented log page {:04X} for MODE SENSE (10)", log_page_code);
            exit(1);
    }

    return {MODE_SENSE_SIZE};
}

enum FormatCode {
    FORMAT_CODE_PHYSICAL_FORMAT = 0x00,
};

static ScsiRetval scsi_command_read_disc_stucture() {
    // Only for format 0
    constexpr u16 READ_DISC_STRUCTURE_SIZE = 0x20;

    logger->debug("SCSI READ_DISC_STRUCTURE");

    const u8 media_type = in_params[1] & 0xF;
    const u32 addr  = (in_params[2] << 24) | (in_params[3] << 16) | (in_params[4] << 8) | in_params[5];
    const u8 layer  = in_params[6];
    const u8 format = in_params[7];

    assert(media_type == 0);
    assert(addr == 0);
    assert(layer == 0);
    
    switch (format) {
        case FormatCode::FORMAT_CODE_PHYSICAL_FORMAT: {
            const u16 data_length = READ_DISC_STRUCTURE_SIZE + sizeof(u32);

            const u32 first_sector = 0x30000;
            const u32 last_sector  = first_sector + Umd::NUM_SECTORS_DUAL - 1;

            logger->trace("PHYSICAL_FORMAT");

            write_out_fifo(data_length >> 8);
            write_out_fifo(data_length >> 0);
            // Reserved
            write_out_fifo(0);
            write_out_fifo(0);
            // The following values are taken from JPCSP until I can send disc commands on my PSP
            // Disc type
            write_out_fifo(0x80);
            // Disc size/rate
            write_out_fifo(0);
            // Layer count/type
            // Dual-layer UMDs probably return something else here...
            write_out_fifo(1);
            // Layer density
            write_out_fifo(0xE0);
            write_out_fifo(0);
            // First sector
            write_out_fifo((u8)(first_sector >> 16));
            write_out_fifo((u8)(first_sector >> 8));
            write_out_fifo((u8)(first_sector >> 0));
            write_out_fifo(0);
            // Last sector
            write_out_fifo((u8)(last_sector >> 16));
            write_out_fifo((u8)(last_sector >> 8));
            write_out_fifo((u8)(last_sector >> 0));
            write_out_fifo(0);
            // Last sector in layer 0?
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(0);
            write_out_fifo(7);

            for (u16 i = 22; i < data_length; i++) {
                write_out_fifo(0);
            }
            break;
        }
        default:
            logger->error("Unimplemented disc structure format code {:02X}", format);
            exit(1);
    }

    return {(u16)out_fifo.size()};
}

static ScsiRetval scsi_command_f0() {
    // MAYBE this is the allocation length?
    const u8 data_length = in_params[1];

    logger->warn("SCSI F0");

    assert(data_length == 1);

    // According to JPCSP, this can return one of four values
    write_out_fifo(0x47);

    return {data_length};
}

static ScsiRetval scsi_command_f1() {
    logger->warn("SCSI F1");

    // This command needs to trigger a DATA interrupt, but the kernel also doesn't
    // read any of the data it returns...
    return {1};
}

static ScsiRetval scsi_command_f7() {
    logger->warn("SCSI F7");

    // No outputs?

    return {};
}

static void end_scsi_command(const int command) {
    ScsiRetval retval;

    switch (command) {
        case ScsiCommand::SCSI_COMMAND_TEST_UNIT_READY:
            retval = scsi_command_test_unit_ready();
            break;
        case ScsiCommand::SCSI_COMMAND_REQUEST_SENSE:
            retval = scsi_command_request_sense();
            break;
        case ScsiCommand::SCSI_COMMAND_INQUIRY:
            retval = scsi_command_inquiry();
            break;
        case ScsiCommand::SCSI_COMMAND_READ_LONG:
            retval = scsi_command_read_long();
            break;
        case ScsiCommand::SCSI_COMMAND_MODE_SELECT_LONG:
            retval = scsi_command_mode_select_long();
            break;
        case ScsiCommand::SCSI_COMMAND_MODE_SENSE_LONG:
            retval = scsi_command_mode_sense_long();
            break;
        case ScsiCommand::SCSI_COMMAND_READ_DISC_STRUCTURE:
            retval = scsi_command_read_disc_stucture();
            break;
        case 0xF0:
            retval = scsi_command_f0();
            break;
        case 0xF1:
            retval = scsi_command_f1();
            break;
        case 0xF7:
            retval = scsi_command_f7();
            break;
        default:
            logger->error("Unimplemented SCSI command {:02X}", command);
            exit(1);
    }

    assert_scsi_interrupt(retval);
}

static void start_scsi_command() {
    assert(!HW_ATAPI_STATUS.busy);

    get_in_params();

    const u8 command = in_params[0];

    HW_ATAPI_STATUS.busy = 1;

    scheduler::schedule_event(
        scheduler::EventType::ATAPI,
        end_scsi_command,
        command,
        scheduler::from_microseconds(1000),
        true
    );
}

static void ata_command_packet() {
    logger->debug("ATA PACKET");

    HW_ATAPI_STATUS.data_request = 1;

    set_reason(true, false);
}

static void end_ata_command(const int command) {
    switch (command) {
        case AtaCommand::ATA_COMMAND_PACKET:
            ata_command_packet();
            state_transition(AtapiState::ATAPI_STATE_AWAIT_PACKET);
            break;
        default:
            logger->error("Unimplemented ATA command {:02X}", command);
            exit(1);
    }

    HW_ATAPI_STATUS.busy = 0;

    assert_interrupt();
}

static void start_ata_command(const u8 command) {
    assert(!HW_ATAPI_STATUS.busy);

    HW_ATAPI_STATUS.busy = 1;

    scheduler::schedule_event(
        scheduler::EventType::ATAPI,
        end_ata_command,
        command,
        scheduler::from_microseconds(1000),
        true
    );
}

static u32 ahb_read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_AHB_REVISION:
            logger->debug("AHB_REVISION read32");
            return AHB_REVISION;
        case IoAddress::IO_ADDRESS_AHB_PIOCTRL:
            logger->debug("AHB_PIOCTRL read32");
            return HW_ATAPI_AHB_PIOCTRL;
        case ATAPI_AHB_ADDR + 0x040:
            logger->warn("Unmapped AHB read32 @ {:08X}", addr);
            return 0;
        default:
            logger->error("Unmapped AHB read32 @ {:08X}", addr);
            exit(1);
    }
}

static u8 read8(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_REASON:
            logger->debug("REASON read8");
            return HW_ATAPI_REASON.raw;
        case IoAddress::IO_ADDRESS_LBA_LO:
            logger->debug("LBA_LO read8");
            return HW_ATAPI_LBA_LO;
        case IoAddress::IO_ADDRESS_LBA_MID:
            logger->debug("LBA_MID read8");
            return HW_ATAPI_LBA_MID;
        case IoAddress::IO_ADDRESS_LBA_HI:
            logger->debug("LBA_HI read8");
            return HW_ATAPI_LBA_HI;
        case IoAddress::IO_ADDRESS_DRIVE:
            logger->debug("DRIVE read8");
            return HW_ATAPI_DRIVE;
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS read8");
            // This should clear interrupts
            return HW_ATAPI_STATUS.raw;
        case IoAddress::IO_ADDRESS_ALTSTAT:
            logger->debug("ALTSTAT read8");
            return HW_ATAPI_STATUS.raw;
        default:
            logger->error("Unmapped read8 @ {:08X}", addr);
            exit(1);
    }
}

static u16 read16(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA read16");
            return get_out_data();
        default:
            logger->error("Unmapped read16 @ {:08X}", addr);
            exit(1);
    }
}

static void ahb_write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_AHB_CONTROL:
            logger->debug("AHB_CONTROL write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_AHB_RESET:
            logger->debug("AHB_RESET write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_AHB_PIODLY:
            logger->debug("AHB_PIODLY write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_AHB_MDMADLY:
            logger->debug("AHB_MDMADLY write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_AHB_PIOCTRL:
            logger->debug("AHB_PIOCTRL write32 = {:08X}", data);

            HW_ATAPI_AHB_PIOCTRL = data;
            break;
        case IoAddress::IO_ADDRESS_AHB_INTR:
            logger->debug("AHB_INTR write32 = {:08X}", data);

            HW_ATAPI_AHB_INTR = data;
            break;
        case IoAddress::IO_ADDRESS_AHB_DEVSTAT:
            logger->debug("AHB_DEVSTAT write32 = {:08X}", data);

            HW_ATAPI_AHB_DEVSTAT = data;
            break;
        case ATAPI_AHB_ADDR + 0x038:
        case ATAPI_AHB_ADDR + 0x040:
            logger->warn("Unmapped AHB write32 @ {:08X} = {:08X}", addr, data);
            break;
        default:
            logger->error("Unmapped AHB write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

static void write8(const u32 addr, const u8 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_FEATURES:
            logger->debug("FEATURES write8 = {:02X}", data);

            HW_ATAPI_FEATURES = data;
            break;
        case IoAddress::IO_ADDRESS_REASON:
            logger->debug("REASON write8 = {:02X}", data);

            // This probably clears the current interrupt bits?
            HW_ATAPI_REASON.raw &= data;
            break;
        case IoAddress::IO_ADDRESS_LBA_LO:
            logger->debug("LBA_LO write8 = {:02X}", data);

            HW_ATAPI_LBA_LO = data;
            break;
        case IoAddress::IO_ADDRESS_LBA_MID:
            logger->debug("LBA_MID write8 = {:02X}", data);

            HW_ATAPI_LBA_MID = data;
            break;
        case IoAddress::IO_ADDRESS_LBA_HI:
            logger->debug("LBA_HI write8 = {:02X}", data);

            HW_ATAPI_LBA_HI = data;
            break;
        case IoAddress::IO_ADDRESS_DRIVE:
            logger->debug("DRIVE write8 = {:02X}", data);

            HW_ATAPI_DRIVE = data;
            break;
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND write8 = {:02X}", data);
            start_ata_command(data);
            break;
        case IoAddress::IO_ADDRESS_PKTEND: {
            logger->debug("PKTEND write8 = {:02X}", data);

            switch (state) {
                case AtapiState::ATAPI_STATE_AWAIT_PACKET:
                    start_scsi_command();
                    break;
                case AtapiState::ATAPI_STATE_AWAIT_DATA:
                    assert(ctx.transfer_from_host);
                    // Until we need to emulate host->device commands,
                    // we can just raise the interrupt here
                    clear_fifos();
                    assert_transfer_end_interrupt();
                    break;
                default:
                    logger->error("Invalid state for PACKET end {}", (int)state);
                    exit(1);
            }
            break;
        }
        case IoAddress::IO_ADDRESS_DEVCTRL:
            logger->debug("DEVCTRL write8 = {:02X}", data);

            HW_ATAPI_DEVCTRL.raw = data;

            if (HW_ATAPI_DEVCTRL.software_reset) {
                logger->debug("Soft reset");
                soft_reset();
            }
            break;
        default:
            logger->error("Unmapped write8 @ {:08X} = {:02X}", addr, data);
            exit(1);
    }
}

static void write16(const u32 addr, const u16 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA write16 = {:04X}", data);

            assert(state != AtapiState::ATAPI_STATE_IDLE);
            
            in_fifo.push(data);
            break;
        default:
            logger->error("Unmapped write16 @ {:08X} = {:04X}", addr, data);
            exit(1);
    }
}

void initialize(const char* umd_path) {
    logger = spdlog::stdout_color_st("ATAPI");

    std::memset(&ctx, 0, sizeof(ctx));

    if (umd.mount(umd_path)) {
        logger->debug("UMD inserted");
    }
}

void soft_reset() {
    // Properly reset all status bits
    HW_ATAPI_STATUS.raw = 0;
    HW_ATAPI_STATUS.device_ready = 1;

    // Set packet device signature
    HW_ATAPI_REASON.raw = 1;
    HW_ATAPI_LBA = 0xEB1401;

    clear_fifos();
    state_transition(AtapiState::ATAPI_STATE_IDLE);
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read8_func   = read8,
        .read16_func  = read16,
        .write8_func  = write8,
        .write16_func = write16,
    };

    const bus::PageDescriptor ahb_page_desc {
        .read32_func  = ahb_read,
        .write32_func = ahb_write,
    };

    soft_reset();

    kanacore::get_sc_bus_ptr()->map(ATAPI_ADDR, ATAPI_SIZE, page_desc);
    kanacore::get_sc_bus_ptr()->map(ATAPI_AHB_ADDR, ATAPI_SIZE, ahb_page_desc);
}

void shutdown() {

}

void assert_transfer_end_interrupt() {
    logger->info("Transfer end interrupt");

    HW_ATAPI_STATUS.data_request = 0;
    HW_ATAPI_STATUS.device_ready = 1;

    set_reason(true, true);
    
    HW_ATAPI_STATUS.busy = 0;

    assert_interrupt();
    state_transition(AtapiState::ATAPI_STATE_IDLE);
}

// Called upon LEPTON initialization
void umd_initialize(const int) {
    if (!umd.is_mounted()) {
        gpio::clear_pin(gpio::Pin::PIN_UMD_INSERTED);

        kanacore::release_button(kanacore::Button::BUTTON_UMD);
    } else {
        gpio::set_pin(gpio::Pin::PIN_UMD_INSERTED);

        kanacore::press_button(kanacore::Button::BUTTON_UMD);
    }
}

void read_sectors(std::vector<u8>& sector_bytes) {
    const u16 num_sectors  = umd.get_num_sectors();
    const u32 total_length = Umd::SECTOR_SIZE * num_sectors;

    assert(sector_bytes.size() == total_length);

    for (u16 i = 0; i < num_sectors; i++) {
        umd.seek();
        umd.read(sector_bytes.data() + i * Umd::SECTOR_SIZE);
    }
}

};
