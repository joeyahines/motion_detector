# Motion Detector
A motion detector made for ELEC 7450: Digital Image Processing.

Video is taken from a video source using [Video for Linux](https://www.linuxtv.org/). Video is displayed using
[SDL](https://www.libsdl.org/).

## Requirements
* CMake 3.15
* SDL 2.0
* Linux system
* A V4L Source

## Building Motion Detector
```bash
git clone https://github.com/joeyahines/motion_detector
cd motion_detector
mkdir build
cd build
cmake ../
make 
```

## Running
To run in Motion Detector Mode:
```bash
./motion_detector /dev/video0
```

To run in Test Mode:
```bash
./motion_detector_test /path/to/CDNET/dat number_of_frames
```

