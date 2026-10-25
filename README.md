# 🚀 LibFPrint (Custom Fork for ElanTech 04f3:0c4f via SIFT OpenCV)

![Linux](https://img.shields.io/badge/OS-Linux-informational?style=flat&logo=linux&logoColor=white&color=2bbc8a)
![C++](https://img.shields.io/badge/Language-C++-blue?style=flat&logo=c%2B%2B)
![OpenCV](https://img.shields.io/badge/Library-OpenCV-green?style=flat&logo=opencv)
![Status](https://img.shields.io/badge/Status-Working_Perfectly-success)

> **⚠️ NOTE:** This is a customized fork of the official [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint) v1.94.100 repository.

This repository provides a working, lightning-fast, and highly accurate fingerprint driver for ElanTech sensors (specifically `04f3:0c4f`) on Linux. It completely bypasses the broken default `libfprint` image-matching algorithm which normally fails on this sensor due to its tiny 80x80 pixel matrix.

Instead, this fork handles raw USB communication and uses **OpenCV (SIFT)** for biometric authentication, turning the sensor into a robust biometric device using computer vision.

## ✨ Improvements in this fork
- **Raw USB Capture:** Uses `libusb` to send exact hex commands to wake up the sensor and grab the raw 16-bit 80x80 image buffer.
- **SIFT Feature Extraction:** Uses OpenCV's Scale-Invariant Feature Transform (SIFT) to find unique micro-patterns, completely invariant to finger rotation.
- **Auto-Calibrated Noise Filtering:** Dynamically calculates sensor baseline noise on every run to perfectly detect light touches and lift-offs.
- **Settling Window:** Instead of grabbing the first blurry frame on contact, the driver continuously reads frames over a 600ms settling window and keeps the frame with the absolute best finger contact.
- **Cross-Check Matcher:** Verification uses a Strict Mutual Cross-Check BFMatcher with $L_2$ Euclidean distance filtering to ensure near-0% false acceptance rate.

## 🛠️ Step-by-Step Installation (Arch Linux / EndeavourOS)

### 1. Install Required Dependencies
Install the required development toolchain, OpenCV, and cryptographic/system libraries:
```bash
sudo pacman -S --needed base-devel git meson ninja libusb glib2 pixman libgusb nss polkit libgudev opencv vtk fprintd cmake
```

### 2. Clone and Build
Build the driver using meson and ninja. Passing -Ddoc=false avoids build failures caused by missing documentation scanners or unlinked VTK symbols:

```bash
git clone https://github.com/jomitrix/acer-elanttech-linux.git
cd acer-elanttech-linux

# Configure with system prefix to cleanly replace stock libfprint
meson setup build --prefix=/usr -Ddoc=false

# Compile and install
ninja -C build
sudo ninja -C build install
```

### 3. Disable USB Autosuspend (Mandatory)
Kernel power-management frequently suspends the Elan sensor, resulting in device-disconnected timeouts or stalled verification attempts.
Make permanent continuous power mode for vendor 04f3 / product 0c4f:

```bash
echo 'ACTION=="add", SUBSYSTEM=="usb", ATTR{idVendor}=="04f3", ATTR{idProduct}=="0c4f", ATTR{power/control}="on"' | sudo tee /etc/udev/rules.d/99-elan-fingerprint.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### 4. Lock Pacman Upgrades
Prevent future system updates from overwriting the compiled driver with upstream stock binaries:

Open `/etc/pacman.conf` in your text editor and in the `[options]` section set:
```bash
IgnorePkg = libfprint
```

### 5. Reset Storage & Enroll Fingerprint
Clear legacy corrupted templates, restart the daemon, and enroll your right index finger:

```bash
sudo rm -rf /var/lib/fprint/*
sudo systemctl restart fprintd

fprintd-delete "$USER"
fprintd-enroll -f right-index-finger
```

## 🐞 Troubleshooting & Debugging
If the sensor fails to react to touch or verification results look abnormal, run the daemon manually in verbose mode:

```bash
sudo systemctl stop fprintd
sudo G_MESSAGES_DEBUG=all /usr/lib/fprintd
```
In a secondary terminal window, invoke fprintd-verify.
The primary window will display the captured noise floor, live keypoint count, top descriptor distances, and match verdict in real time.

---

<div align="center">

# Original LibFPrint

*LibFPrint is part of the **[FPrint][Website]** project.*

</div>

## History
**LibFPrint** was originally developed as part of an academic project at the **University Of Manchester**. It aimed to hide the differences between consumer fingerprint scanners and provide a single uniform API to application developers.

[Website]: https://fprint.freedesktop.org/
