/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/kirk.cpp - KIRK crypto engine */

#include <core/hw/kirk.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <cryptopp/aes.h>
#include <cryptopp/cmac.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/modes.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::kirk {

using namespace common;

constexpr u64 KIRK_ADDR = 0x1DE00000;
constexpr u64 KIRK_SIZE = 0x1000;

constexpr u64 AES_KEY_SIZE = 16;

enum IoAddress {
    IO_ADDRESS_PHASE   = KIRK_ADDR + 0x00C,
    IO_ADDRESS_COMMAND = KIRK_ADDR + 0x010,
    IO_ADDRESS_RESULT  = KIRK_ADDR + 0x014,
    IO_ADDRESS_STATUS  = KIRK_ADDR + 0x01C,
    IO_ADDRESS_STATEND = KIRK_ADDR + 0x028,
    IO_ADDRESS_SRCADDR = KIRK_ADDR + 0x02C,
    IO_ADDRESS_DSTADDR = KIRK_ADDR + 0x030,
};

#define HW_KIRK_PHASE   ctx.phase
#define HW_KIRK_COMMAND ctx.command
#define HW_KIRK_RESULT  ctx.result
#define HW_KIRK_STATUS  ctx.status
#define HW_KIRK_SRCADDR ctx.source_addr
#define HW_KIRK_DSTADDR ctx.destination_addr

enum KirkCommand {
    KIRK_COMMAND_DECRYPT_PRIVATE = 0x01,
};

enum AuthMode {
    AUTH_MODE_CMAC,
    AUTH_MODE_ECDSA,
};

enum KirkError {
    KIRK_ERROR_OK,
};

static std::shared_ptr<spdlog::logger> logger;

static struct {
    // KIRK phase register
    union {
        u8 raw;

        struct {
            u8 first_phase  : 1;
            u8 second_phase : 1;
            u8              : 6;
        };
    } phase;

    u32 command;
    u32 result;

    // KIRK status
    union {
        u8 raw;

        struct {
            u8 phase_done         : 1;
            u8 phase_error        : 1;
            u8                    : 2;
            u8 needs_second_phase : 1;
            u8                    : 3;
        };
    } status;

    u32 source_addr;
    u32 destination_addr;
} ctx;

static inline u32 align_up(const u32 data) {
    constexpr u32 ALIGNMENT = 16;

    if ((data & (ALIGNMENT - 1)) != 0) {
        return (data | (ALIGNMENT - 1)) + 1;
    }

    return data;
}

static void dma_read(const u32 addr, u8* data, const u32 size) {
    // Sad, let's optimize this at some point
    for (u32 i = 0; i < size; i++) {
        data[i] = bus::read<u8>(addr + i);
    }
}

static void dma_write(const u32 addr, const u8* data, const u32 size) {
    // See above
    for (u32 i = 0; i < size; i++) {
        bus::write<u8>(addr + i, data[i]);
    }
}

static void print_key(const char* name, const u8* key) {
    logger->debug(
        "{}: {:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
        name,
        key[0x0], key[0x1], key[0x2], key[0x3], key[0x4], key[0x5], key[0x6], key[0x7],
        key[0x8], key[0x9], key[0xA], key[0xB], key[0xC], key[0xD], key[0xE], key[0xF]
    );
}

// Decrypts AES-CBC encrypted data in-place
static void aes_decrypt(const u8* key, u8* data, const u32 size) {
    constexpr u8 AES_ZERO_IV[] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    };

    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption(key, AES_KEY_SIZE, AES_ZERO_IV).ProcessData(data, data, size);
}

static bool cmac_verify(const u8* key, u8* data, const u32 size, const u8* expected_hash) {
    // Compute the CMAC hash
    u8 hash[AES_KEY_SIZE];

    auto cmac = CryptoPP::CMAC<CryptoPP::AES>(key, AES_KEY_SIZE);

    cmac.Update(data, size);
    cmac.Final(hash);

    print_key("CMAC hash", hash);
    print_key(" Expected", expected_hash);

    return std::memcmp(hash, expected_hash, AES_KEY_SIZE) == 0;
}

static i32 command_decrypt_private() {
    // The decryption and CMAC keys are encrypted using this key
    constexpr u8 AES_MASTER_KEY[] = {
        0x98, 0xC9, 0x40, 0x97, 0x5C, 0x1D, 0x10, 0xE8,
        0x7F, 0xE6, 0x0E, 0xA3, 0xFD, 0x03, 0xA8, 0xBA
    };

    constexpr u64 METADATA_SIZE = 0x90;

    logger->debug("DECRYPT_PRIVATE");

    // Step one is to get the metadata header, which includes
    // keys, mode and more
    u32 metadata[METADATA_SIZE / sizeof(u32)];

    dma_read(HW_KIRK_SRCADDR, (u8*)metadata, METADATA_SIZE);

    // TODO: use named constants

    assert(metadata[0x19] == AUTH_MODE_CMAC);

    u8* decrypt_key = (u8*)&metadata[0];
    u8* cmac_key = (u8*)&metadata[4];

    // Decrypt the decryption and CMAC keys
    aes_decrypt(AES_MASTER_KEY, decrypt_key, 2 * AES_KEY_SIZE);

    print_key("Decryption key", decrypt_key);
    print_key("Signature  key", cmac_key);

    // Now we verify the signature of the header
    logger->debug("Verifying header CMAC");

    if (!cmac_verify(cmac_key, (u8*)&metadata[0x18], METADATA_SIZE - 0x60, (u8*)&metadata[8])) {
        logger->error("Header CMAC check failed");
        exit(1);
    }

    // To check the data CMAC, we first get the entire input buffer

    // The data size is always aligned up when not on a 16-byte boundary
    const u32 data_size = align_up(metadata[0x1C]);
    const u32 padding = metadata[0x1D];

    const u32 data_offset = METADATA_SIZE + padding;
    const u32 total_size = data_offset + data_size;
    
    std::vector<u8> buf(total_size);

    dma_read(HW_KIRK_SRCADDR, buf.data(), total_size);

    logger->debug("Verifying data CMAC");

    if (!cmac_verify(cmac_key, &buf[0x60], total_size - 0x60, (u8*)&metadata[12])) {
        logger->error("Data CMAC check failed");
        exit(1);
    }

    // NOW we can decrypt the data
    aes_decrypt(decrypt_key, &buf[data_offset], data_size);
    dma_write(HW_KIRK_DSTADDR, &buf[data_offset], data_size);

    HW_KIRK_STATUS.needs_second_phase = false;

    return KIRK_ERROR_OK;
}

static void start_first_phase() {
    i32 result;

    switch (HW_KIRK_COMMAND) {
        case KirkCommand::KIRK_COMMAND_DECRYPT_PRIVATE:
            result = command_decrypt_private();
            break;
        default:
            logger->error("Unimplemented command {:02X}", HW_KIRK_COMMAND);
            exit(1);
    }

    // TODO: delay command completion
    HW_KIRK_STATUS.phase_done  = true;
    HW_KIRK_STATUS.phase_error = result != KirkError::KIRK_ERROR_OK;

    HW_KIRK_RESULT = result;
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_RESULT:
            logger->debug("RESULT read32");
            return HW_KIRK_RESULT;
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS read32");
            return HW_KIRK_STATUS.raw;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_PHASE:
            logger->debug("PHASE write32 = {:08X}", data);

            HW_KIRK_PHASE.raw = data;

            if (HW_KIRK_PHASE.first_phase) {
                start_first_phase();
            }

            if (HW_KIRK_PHASE.second_phase) {
                logger->error("Unimplemented second phase");
                exit(1);
            }
            break;
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND write32 = {:08X}", data);

            HW_KIRK_COMMAND = data;
            break;
        case IoAddress::IO_ADDRESS_STATEND:
            // Not sure if this does anything that's visible to SC
            logger->debug("STATEND write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_SRCADDR:
            logger->debug("SRCADDR write32 = {:08X}", data);

            HW_KIRK_SRCADDR = data;
            break;
        case IoAddress::IO_ADDRESS_DSTADDR:
            logger->debug("DSTADDR write32 = {:08X}", data);

            HW_KIRK_DSTADDR = data;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("KIRK");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, KIRK I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    bus::map(KIRK_ADDR, KIRK_SIZE, page_desc);
}

void shutdown() {

}

};
