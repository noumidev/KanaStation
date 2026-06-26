# KanaStation - PlayStation Portable emulator

## Progress
- boots PSP-1000 boot ROM
- boots OFW 1.50 IPL
- boots kernel up until MeBooter

This is a **work-in-progress** and very early!

## Usage
`KanaStation [path to boot ROM] [path to NAND dump] (optional: [path to MS image])`

## Build instructions
```
git clone --recursive https://github.com/noumidev/KanaStation
cd KanaStation
mkdir build && cd build
cmake ..
make
```
