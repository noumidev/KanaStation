# KanaStation - PlayStation Portable emulator
KanaStation is a low-level PlayStation Portable emulator aiming to emulate as much of the underlying hardware as possible.

## Progress
- boots PSP-1000 boot ROM
- boots OFW 1.50 IPL (and some custom ones)
- boots OFW 1.50
- boots VSH/XMB

This is a **work-in-progress** and very early!

## Usage
Drop the provided `config.toml` in the same folder as the executable and change the following things:
- `fuse_id` is a 48-bit identifier and unique to every console. Per-console cryptography depends on this identifier and is not optional. You can find it by running software like `psp_ident` on your console.
- `mobo_type` is the motherboard type of your console and identifies the model (1000, 2000, ...). Like `fuse_id`, you can find it by running `psp_ident` on your console. The only supported value as of right now is `"TA-082"`, a specific model of PSP-1000 (the model I'm developing the emulator with). The emulator will support more in the future.
- `service_mode` boots the emulator into service mode, allowing you to run custom IPL payloads from a Memory Stick image. This should be `false` in 99.99% of cases.
- Set up all paths. Boot ROM and NAND are required, UMD and Memory Stick are optional.

## Build instructions
```
git clone --recursive https://github.com/noumidev/KanaStation
cd KanaStation
mkdir build && cd build
cmake .. -DCRYPTOPP_BUILD_TESTING=OFF
make
```

## Screenshots
<img width="592" height="416" alt="image" src="https://github.com/user-attachments/assets/2c9aed89-b0f8-4c8c-8cdc-d6cf47abc260" />
<img width="592" height="416" alt="image" src="https://github.com/user-attachments/assets/6686b226-9708-4292-b0c1-f0bec50beef0" />
<img width="592" height="416" alt="image" src="https://github.com/user-attachments/assets/6356cffd-27d9-4b5b-a751-b12bb607343f" />
