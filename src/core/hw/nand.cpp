/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/nand.cpp - NAND interface */

#include <core/hw/nand.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::nand {

using namespace common;

constexpr u64 NAND_INTERFACE_ADDR = 0x1D101000;
constexpr u64 NAND_INTERFACE_SIZE = 0x1000;

constexpr u64 DMA_BUFFER_ADDR = 0x1FF00000;
constexpr u64 DMA_BUFFER_SIZE = 0x1000;

// NAND pages (data area + spare area)
constexpr u64 PAGE_SIZE           = 512;
constexpr u64 PAGE_MASK           = PAGE_SIZE - 1;
constexpr u64 PAGE_SIZE_WITH_ECC  = PAGE_SIZE + 16;
constexpr u64 NUM_PAGES_PER_BLOCK = 32;

constexpr u64 BLOCK_SIZE = NUM_PAGES_PER_BLOCK * PAGE_SIZE_WITH_ECC;
constexpr u64 NUM_BLOCKS = 2048;
constexpr u64 NAND_SIZE  = NUM_BLOCKS * BLOCK_SIZE;

enum IoAddress {
    IO_ADDRESS_STATUS  = NAND_INTERFACE_ADDR + 0x004,
    IO_ADDRESS_COMMAND = NAND_INTERFACE_ADDR + 0x008,
    IO_ADDRESS_RESET   = NAND_INTERFACE_ADDR + 0x014,
    IO_ADDRESS_DMAPAGE = NAND_INTERFACE_ADDR + 0x020,
    IO_ADDRESS_DMACTRL = NAND_INTERFACE_ADDR + 0x024,
    IO_ADDRESS_DMASTAT = NAND_INTERFACE_ADDR + 0x028,
};

#define HW_NAND_STATUS  ctx.status
#define HW_NAND_DMAPAGE ctx.dma.page
#define HW_NAND_DMACTRL ctx.dma.control

enum NandCommand {
    NAND_COMMAND_RESET = 0xFF,
};

enum DmaDirection {
    DMA_DIRECTION_TO_RAM,
    DMA_DIRECTION_TO_NAND,
};

static std::shared_ptr<spdlog::logger> logger;

static struct {
    // NAND interface status register
    union {
        u8 raw;

        struct {
            u8 ready           : 1;
            u8                 : 6;
            u8 write_protected : 1;
        };
    } status;

    // NAND DMA related registers
    struct {
        // Page to be read/written
        u32 page;

        union {
            u16 raw;

            struct {
                u16 busy           : 1;
                u16 direction      : 1; // 1 = to NAND
                u16                : 6;
                u16 transfer_data  : 1;
                u16 transfer_spare : 1;
                u16                : 6;
            };
        } control;
    } dma;
} ctx;

static std::array<u8, NAND_SIZE> nand;

// Used for NAND DMA, mapped to 0x1FF00xxx
static struct {
    u32 data_area [PAGE_SIZE / sizeof(u32)];
    u32 spare_area[(PAGE_SIZE_WITH_ECC - PAGE_SIZE) / sizeof(u32)];
} dma_buffer;

static inline void reset_nand_status() {
    HW_NAND_STATUS.ready = true;
}

// For use with command RESET
static void reset_nand_chip() {
    // This needs to reset all chip state
    reset_nand_status();
}

// Writing to the NAND interface RESET register does this
static void reset_nand_interface() {
    // Does this reset the chip?
}

static void command_reset() {
    logger->debug("RESET");

    // This should abort currently active commands as well, for now
    // let's just make sure this doesn't happen in the first place
    assert(HW_NAND_STATUS.ready);

    reset_nand_chip();
}

static void start_command(const u32 command) {
    switch (command) {
        case NandCommand::NAND_COMMAND_RESET:
            command_reset();
            break;
        default:
            logger->error("Unimplemented command {:02X}", command);
            exit(1);
    }

    // TODO: delay command completion
}

static void start_dma() {
    const bool is_write = HW_NAND_DMACTRL.direction == DmaDirection::DMA_DIRECTION_TO_NAND;

    // Determines whether to transfer the data area, the spare area, or both
    const bool transfer_data = HW_NAND_DMACTRL.transfer_data != 0;
    const bool transfer_spare = HW_NAND_DMACTRL.transfer_spare != 0;

    logger->debug(
        "DMA transfer (page: {:04X}, to NAND: {}, data: {}, spare: {})",
        HW_NAND_DMAPAGE,
        is_write,
        transfer_data,
        transfer_spare
    );

    if (is_write) {
        logger->error("Unimplemented NAND page program via DMA");
        exit(1);
    }

    const u64 nand_offset = HW_NAND_DMAPAGE * PAGE_SIZE_WITH_ECC;

    if (transfer_data) {
        std::memcpy(dma_buffer.data_area, nand.data() + nand_offset, PAGE_SIZE);
    }

    if (transfer_spare) {
        std::memcpy(dma_buffer.spare_area, nand.data() + nand_offset + PAGE_SIZE, PAGE_SIZE_WITH_ECC - PAGE_SIZE);
    }

    // TODO: delay command completion
    HW_NAND_DMACTRL.busy = false;
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS read32");
            return HW_NAND_STATUS.raw;
        case IoAddress::IO_ADDRESS_DMACTRL:
            logger->debug("DMACTRL read32");
            return HW_NAND_DMACTRL.raw;
        case IoAddress::IO_ADDRESS_DMASTAT:
            logger->debug("DMASTAT read32");

            // This returns an error code upon DMA failure, but
            // I don't know what possible error codes look like
            return 0;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static u32 read_dma_buffer(const u32 addr) {
    // TODO: use named constants
    u32 data;

    if ((addr & ~PAGE_MASK) == DMA_BUFFER_ADDR) {
        data = dma_buffer.data_area[(addr & PAGE_MASK) / sizeof(u32)];
    } else {
        switch (addr) {
            case DMA_BUFFER_ADDR + 0x800:
                // See notes on ECC above
                data = dma_buffer.spare_area[0];
                break;
            case DMA_BUFFER_ADDR + 0x900:
                data = dma_buffer.spare_area[1];
                break;
            case DMA_BUFFER_ADDR + 0x904:
                data = dma_buffer.spare_area[2];
                break;
            case DMA_BUFFER_ADDR + 0x908:
                data = dma_buffer.spare_area[3];
                break;
            default:
                logger->error("Unmapped read32 @ {:08X}", addr);
                exit(1);
        }
    }

    logger->debug("DMA buffer read32 @ {:08X}", addr);
    return data;
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND write32 = {:08X}", data);

            start_command(data);
            break;
        case IoAddress::IO_ADDRESS_RESET:
            logger->debug("RESET write32 = {:08X}", data);

            if ((data & 1) != 0) {
                reset_nand_interface();
            }
            break;
        case IoAddress::IO_ADDRESS_DMAPAGE:
            logger->debug("DMAPAGE write32 = {:08X}", data);

            HW_NAND_DMAPAGE = data >> 10;
            break;
        case IoAddress::IO_ADDRESS_DMACTRL:
            logger->debug("DMACTRL write32 = {:08X}", data);

            HW_NAND_DMACTRL.raw = data;

            if (HW_NAND_DMACTRL.busy) {
                start_dma();
            }
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

static void write_dma_buffer(const u32 addr, const u32 data) {
    // TODO: use named constants
    if ((addr & ~PAGE_MASK) == DMA_BUFFER_ADDR) {
        dma_buffer.data_area[(addr & PAGE_MASK) / sizeof(u32)] = data;
    } else {
        switch (addr) {
            case DMA_BUFFER_ADDR + 0x800:
                dma_buffer.spare_area[0] = data;
                break;
            case DMA_BUFFER_ADDR + 0x900:
                dma_buffer.spare_area[1] = data;
                break;
            case DMA_BUFFER_ADDR + 0x904:
                dma_buffer.spare_area[2] = data;
                break;
            case DMA_BUFFER_ADDR + 0x908:
                dma_buffer.spare_area[3] = data;
                break;
            default:
                logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
                exit(1);
        }
    }

    logger->debug("DMA buffer write32 @ {:08X} = {:08X}", addr, data);
}

void initialize(const char* nand_path) {
    logger = spdlog::stdout_color_st("NAND");

    // Load NAND image
    FILE* file = std::fopen(nand_path, "rb");

    if (file == nullptr) {
        logger->error("Can't open NAND image");
        exit(1);
    }

    std::fseek(file, 0, SEEK_END);

    const u64 file_size = std::ftell(file);

    if (file_size != NAND_SIZE) {
        logger->error("Supplied NAND image has invalid size (expected: {}, got: {})", NAND_SIZE, file_size);
        exit(1);
    }

    std::fseek(file, 0, SEEK_SET);

    if (std::fread(nand.data(), sizeof(u8), NAND_SIZE, file) != NAND_SIZE) {
        logger->error("Failed to read NAND image");
        exit(1);
    }

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    reset_nand_chip();
    reset_nand_interface();
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, NAND I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    bus::map(NAND_INTERFACE_ADDR, NAND_INTERFACE_SIZE, page_desc);

    const bus::PageDescriptor dma_page_desc {
        // See above
        .read32_func  = read_dma_buffer,
        .write32_func = write_dma_buffer,
    };

    bus::map(DMA_BUFFER_ADDR, DMA_BUFFER_SIZE, dma_page_desc);

    reset_nand_chip();
    reset_nand_interface();
}

void shutdown() {

}

};
