#include "sift_engine.h"
#include <opencv2/opencv.hpp>
#include <libusb-1.0/libusb.h>
#include <vector>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>

using namespace std;
using namespace cv;

Mat process_image(const Mat& img) {
    Mat blur, out;
    GaussianBlur(img, blur, Size(5, 5), 0);
    Ptr<CLAHE> clahe = createCLAHE(2.0, Size(8, 8));
    clahe->apply(blur, out);
    return out;
}

bool capture_usb_images(vector<Mat>& out_images, int required_touches, int timeout_sec) {
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) < 0) {
        cerr << "\n[SIFT ERROR] Failed to initialize libusb!" << endl;
        return false;
    }

    libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx, 0x04f3, 0x0c4f);
    if (!dev) {
        cerr << "\n[SIFT ERROR] Failed to open device 04f3:0c4f! Check permissions / USB connection." << endl;
        libusb_exit(ctx);
        return false;
    }

    if (libusb_kernel_driver_active(dev, 0) == 1) {
        libusb_detach_kernel_driver(dev, 0);
    }
    libusb_set_configuration(dev, 1);
    libusb_claim_interface(dev, 0);

    int transferred = 0;
    unsigned char wake_cmd[] = {0x40, 0x31};
    libusb_bulk_transfer(dev, 0x01, wake_cmd, sizeof(wake_cmd), &transferred, 1000);
    this_thread::sleep_for(chrono::milliseconds(300));

    unsigned char req1[] = {0x40, 0x3f};
    unsigned char req2[] = {0x00, 0x09};
    unsigned char buffer[12800];

    // Measure baseline sensor noise
    double baseline_stddev = 0;
    int calib_count = 0;
    for (int i = 0; i < 4; i++) {
        libusb_bulk_transfer(dev, 0x01, req1, sizeof(req1), &transferred, 1000);
        libusb_bulk_transfer(dev, 0x01, req2, sizeof(req2), &transferred, 1000);
        int res = libusb_bulk_transfer(dev, 0x82, buffer, sizeof(buffer), &transferred, 1000);
        if (res == 0 && transferred == 12800) {
            Mat raw(80, 80, CV_16UC1, buffer);
            Scalar mean, sdev;
            meanStdDev(raw, mean, sdev);
            baseline_stddev += sdev.val[0];
            calib_count++;
        }
        this_thread::sleep_for(chrono::milliseconds(30));
    }
    if (calib_count > 0) baseline_stddev /= calib_count;
    else baseline_stddev = 200.0;

    double touch_threshold = max(baseline_stddev + 400.0, 700.0);
    double lift_threshold  = baseline_stddev + 200.0;

    int touches = 0;
    bool waiting_for_off = false;
    auto start_time = chrono::steady_clock::now();

    if (required_touches > 1) {
        cerr << "\n========================================================" << endl;
        cerr << "  >>> [SIFT ENROLL] Stage 1/" << required_touches << ": PLACE your finger on the sensor..." << endl;
        cerr << "  >>> (Sensor noise baseline: " << (int)baseline_stddev << " | Touch trigger: >" << (int)touch_threshold << " | Lift trigger: <" << (int)lift_threshold << ")" << endl;
        cerr << "========================================================" << endl;
    } else {
        cerr << "\n  >>> [SIFT VERIFY] PLACE your finger on the sensor..." << endl;
    }

    int poll_count = 0;
    while (touches < required_touches && 
           chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start_time).count() < timeout_sec) {
        
        int r1 = libusb_bulk_transfer(dev, 0x01, req1, sizeof(req1), &transferred, 1000);
        int r2 = libusb_bulk_transfer(dev, 0x01, req2, sizeof(req2), &transferred, 1000);
        
        int res = libusb_bulk_transfer(dev, 0x82, buffer, sizeof(buffer), &transferred, 1000);
        poll_count++;

        // Print debug every 25 polls (~1 second) so we can see what's happening
        if (poll_count % 25 == 1) {
            cerr << "  >>> [SIFT DEBUG] poll #" << poll_count << " usb_r1=" << r1 << " r2=" << r2 << " read=" << res << " bytes=" << transferred;
        }

        if (res == 0 && transferred == 12800) {
            Mat raw(80, 80, CV_16UC1, buffer);
            Scalar mean, stddev;
            meanStdDev(raw, mean, stddev);
            double current_stddev = stddev.val[0];
            
            if (poll_count % 25 == 1) {
                cerr << " noise=" << (int)current_stddev << " thr=" << (int)touch_threshold << endl;
            }

            if (waiting_for_off) {
                if (current_stddev < lift_threshold) {
                    waiting_for_off = false;
                    if (touches < required_touches) {
                        cerr << "  >>> [SIFT ENROLL] Finger lifted (noise=" << (int)current_stddev << ")! -> PLACE finger again for stage " << (touches + 1) << "/" << required_touches << "..." << endl;
                    }
                    this_thread::sleep_for(chrono::milliseconds(250));
                }
            } else {
                if (current_stddev > touch_threshold) {
                    // Finger detected! Now wait ~600ms and capture the best frame (highest stddev = firmest press)
                    cerr << "  >>> [SIFT] Touch detected (noise=" << (int)current_stddev << "), settling..." << endl;
                    Mat best_raw;
                    double best_stddev = current_stddev;
                    normalize(raw, best_raw, 0, 255, NORM_MINMAX, CV_8UC1);
                    
                    auto settle_start = chrono::steady_clock::now();
                    while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - settle_start).count() < 600) {
                        libusb_bulk_transfer(dev, 0x01, req1, sizeof(req1), &transferred, 1000);
                        libusb_bulk_transfer(dev, 0x01, req2, sizeof(req2), &transferred, 1000);
                        int sres = libusb_bulk_transfer(dev, 0x82, buffer, sizeof(buffer), &transferred, 1000);
                        if (sres == 0 && transferred == 12800) {
                            Mat sraw(80, 80, CV_16UC1, buffer);
                            Scalar sm, ss;
                            meanStdDev(sraw, sm, ss);
                            if (ss.val[0] > best_stddev) {
                                best_stddev = ss.val[0];
                                normalize(sraw, best_raw, 0, 255, NORM_MINMAX, CV_8UC1);
                            }
                        }
                        this_thread::sleep_for(chrono::milliseconds(30));
                    }
                    
                    out_images.push_back(best_raw);
                    touches++;
                    waiting_for_off = true;
                    if (required_touches > 1) {
                        cerr << "  >>> [SIFT ENROLL] Touch " << touches << "/" << required_touches << " REGISTERED! (best_noise=" << (int)best_stddev << ") -> LIFT YOUR FINGER NOW" << endl;
                    } else {
                        cerr << "  >>> [SIFT VERIFY] Touch REGISTERED! (best_noise=" << (int)best_stddev << ")" << endl;
                    }
                }
            }
        } else {
            if (poll_count % 25 == 1) {
                cerr << " FAILED (res=" << res << " bytes=" << transferred << ")" << endl;
            }
        }
        this_thread::sleep_for(chrono::milliseconds(40));
    }

    if (touches >= required_touches) {
        if (required_touches > 1) {
            cerr << "  >>> [SIFT ENROLL] All " << required_touches << " touches captured successfully! Extracting features..." << endl;
        }
    } else {
        cerr << "  >>> [SIFT WARNING] Timed out with only " << touches << "/" << required_touches << " touches." << endl;
    }

    libusb_release_interface(dev, 0);
    libusb_close(dev);
    libusb_exit(ctx);
    
    return touches > 0;
}

extern "C" {

int sift_engine_init(void) { return 1; }

int sift_engine_enroll(unsigned char** out_data) {
    vector<Mat> images;
    // 50-second timeout allows ample time for 5 distinct touch/lift cycles
    if (!capture_usb_images(images, 5, 50)) return 0;

    Ptr<SIFT> sift = SIFT::create();
    Mat super_des;

    for (size_t i = 0; i < images.size(); i++) {
        Mat clean_img = process_image(images[i]);
        vector<KeyPoint> kp;
        Mat des;
        sift->detectAndCompute(clean_img, noArray(), kp, des);
        cerr << "  >>> [SIFT] Touch Frame " << (i + 1) << ": found " << kp.size() << " keypoints" << endl;

        if (!des.empty() && kp.size() >= 5) {
            if (super_des.empty()) {
                super_des = des;
            } else {
                vconcat(super_des, des, super_des);
            }
        }
    }

    if (super_des.empty()) {
        cerr << "  >>> [SIFT ERROR] Failed to extract valid SIFT keypoints from touches." << endl;
        return 0;
    }

    int size = super_des.total() * super_des.elemSize();
    int header_size = 3 * sizeof(int);
    int total_size = header_size + size;
    
    *out_data = (unsigned char*)malloc(total_size);
    int* header = (int*)(*out_data);
    header[0] = super_des.rows;
    header[1] = super_des.cols;
    header[2] = super_des.type();
    
    memcpy(*out_data + header_size, super_des.data, size);
    cerr << "  >>> [SIFT ENROLL SUCCESS] Enrolled " << super_des.rows << " descriptors (" << total_size << " bytes)" << endl;
    cerr << "========================================================\n" << endl;
    return total_size;
}

int sift_engine_verify(const unsigned char* saved_data, int data_size) {
    if (data_size < 12) return 0;

    const int* header = (const int*)saved_data;
    int rows = header[0], cols = header[1], type = header[2];
    
    if (12 + (rows * cols * CV_ELEM_SIZE(type)) != data_size) return 0;
    Mat saved_des(rows, cols, type);
    memcpy(saved_des.data, saved_data + 12, data_size - 12);

    vector<Mat> images;
    if (!capture_usb_images(images, 1, 15)) return 0;

    Mat clean_img = process_image(images[0]);
    Ptr<SIFT> sift = SIFT::create();
    vector<KeyPoint> kp;
    Mat current_des;
    sift->detectAndCompute(clean_img, noArray(), kp, current_des);
    cerr << "  >>> [SIFT VERIFY] Live scan: found " << kp.size() << " keypoints (comparing against " << rows << " enrolled descriptors)" << endl;

    if (current_des.empty() || kp.size() < 5) {
        cerr << "  >>> [SIFT VERIFY] Too few keypoints on live scan (" << kp.size() << " < 5)" << endl;
        return 0;
    }

    // Use Cross-Check matching: matches are only kept if descriptors mutually select each other
    BFMatcher matcher(NORM_L2, true);
    vector<DMatch> matches;
    matcher.match(current_des, saved_des, matches);

    // Sort matches by distance (best to worst)
    sort(matches.begin(), matches.end(), [](const DMatch& a, const DMatch& b) {
        return a.distance < b.distance;
    });

    int good_matches = 0;
    double total_dist = 0;
    for (const auto& m : matches) {
        // Natural SIFT distance range on 80x80 sensor
        if (m.distance < 300.0f) {
            good_matches++;
            total_dist += m.distance;
        }
    }

    double avg_dist = good_matches > 0 ? (total_dist / good_matches) : 0;

    cerr << "  >>> [SIFT MATCH] Top match distances: [ ";
    for (size_t i = 0; i < min((size_t)10, matches.size()); i++) {
        cerr << (int)matches[i].distance << " ";
    }
    cerr << "]" << endl;

    cerr << "  >>> [SIFT MATCH] " << good_matches << " confident matches (dist < 300, avg: " 
         << std::fixed << std::setprecision(1) << avg_dist << ", threshold: 6)" << endl;

    bool match = (good_matches >= 6);
    cerr << "  >>> [SIFT RESULT] " << (match ? "MATCH SUCCESS (VERIFIED)" : "MATCH FAILED (REJECTED)") << endl;
    cerr << "========================================================\n" << endl;
    return match ? 1 : 0;
}

}
