# KanaStation - PlayStation Portable emulator
KanaStation is a low-level PlayStation Portable emulator aiming to emulate as much of the underlying hardware as possible.

## Progress
- boots PSP-1000 boot ROM
- boots OFW 1.50 IPL (and some custom ones)
- boots various OFWs
- boots VSH/XMB
- boots some games and homebrew

This is a **work-in-progress** and very early!

## Usage
Drop the provided `config.toml` in the same folder as the executable and change the following things:
- `fuse_id` is a 48-bit identifier and unique to every console. Per-console cryptography depends on this identifier and is not optional. You can find it by running software like `psp_ident` on your console.
- `mobo_type` is the motherboard type of your console and identifies the model (1000, 2000, ...). Like `fuse_id`, you can find it by running `psp_ident` on your console. The only supported values as of right now are `"TA-082"`, a specific model of PSP-1000 (the model I'm developing the emulator with), and `"TA-088"`, a Slim model. The emulator will support more in the future.
- `service_mode` boots the emulator into service mode, allowing you to run custom IPL payloads from a Memory Stick image. This should be `false` in 99.99% of cases.
- `hacks.dmacplus` scans out the GE framebuffer instead of any framebuffer, and ignores transfers to RAM on the SC2ME channel. This prevents certain `sceMpeg` related crashes and black screens, but causes flashing visuals and noisy sounds upon gameboot! Off by default, but required to boot games and homebrew...
- `splines` enables the rendering of B-Splines. They are slow and currently a little broken, but also required for the XMB waves.
- Set up all paths. Boot ROM and NAND are required, UMD and Memory Stick are optional.

## Infos
- changes to NAND and the Memory Stick are not currently saved
- the emulator gets very(!!) slow when 3D is used
- many games and homebrew can't boot yet, and those that do often experience graphical issues, no sound, ...
- PSP-2000/Slim firmware can't boot yet

## Build instructions
```
git clone --recursive https://github.com/noumidev/KanaStation
cd KanaStation
mkdir build && cd build
cmake .. -DCRYPTOPP_BUILD_TESTING=OFF
make
```

## AI policy
None of the code in this repository was written using AI, and I would like to keep it that way. I will not accept any contributions that were AI-assisted.

## Screenshots
<img width="536" height="344" alt="image" src="https://github.com/user-attachments/assets/1ccd0e4d-a8a4-4541-9d54-73bf8189dec0" />
<img width="536" height="344" alt="image" src="https://github.com/user-attachments/assets/3040f4d2-af39-46a9-b807-8ae3b3e68b7a" />
<img width="536" height="344" alt="image" src="https://github.com/user-attachments/assets/85775e42-e202-4124-9cca-dec3429195b5" />
