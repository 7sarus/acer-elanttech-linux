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

## 🛠️ Arch Linux Installation
This repository can be directly cloned and built using `makepkg` or `meson`.

```bash
git clone https://github.com/7sarus/acer-elanttech-linux.git
cd acer-elanttech-linux
meson setup build
ninja -C build
sudo ninja -C build install
sudo systemctl restart fprintd
```

*(You must ensure you have `opencv` and `cmake` installed on your system).*

Make sure to delete any old corrupted fingerprints first:
```bash
fprintd-delete "$USER"
fprintd-enroll -f right-index-finger
```

---

<div align="center">

# Original LibFPrint

*LibFPrint is part of the **[FPrint][Website]** project.*

</div>

## History
**LibFPrint** was originally developed as part of an academic project at the **University Of Manchester**. It aimed to hide the differences between consumer fingerprint scanners and provide a single uniform API to application developers.

[Website]: https://fprint.freedesktop.org/
