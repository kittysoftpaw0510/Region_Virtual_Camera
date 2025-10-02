# RegionCam - Screen Region & Window to Virtual Camera

High-performance C++ application for Windows that captures screen regions or specific windows and streams to OBS Virtual Camera.

## Features

- Interactive Region Selection
- Window Capture (allows overlaying other windows)
- High Performance (DXGI + GDI)
- OBS Virtual Camera Integration
- Configurable FPS (15/30/60)
- Flip/Mirror Support

## Usage

### Screen Region Capture
RegionCam.exe

### Window Capture
RegionCam.exe --list-windows
RegionCam.exe --window "Chrome"

### Options
--fps 60
--x 100 --y 100 --width 1280 --height 720
--vflip / --hflip
--window "Window Title"
--list-windows
--verbose / -v (enable debug logging)
--help / -h (show help)

## Requirements

- Windows 10/11
- OBS Studio (for virtual camera driver)

## Window Capture Benefits

- Other windows can overlay the captured window
- Perfect for presentations with overlays
- Automatically follows window size
- Captures only window content (no borders)

## Credits

OBS Virtual Camera queue from pyvirtualcam
