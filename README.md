# KanaStation - PlayStation Portable emulator

## Progress
- boots PSP-1000 boot ROM
- boots OFW 1.50 IPL
- boots OFW 1.50

This is a **work-in-progress** and very early!

## Usage
`KanaStation [path to boot ROM] [path to NAND dump] (optional: [path to MS image])`

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
