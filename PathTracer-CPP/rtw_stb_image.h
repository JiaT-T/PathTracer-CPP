#pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// Disable strict warnings for this header from the Microsoft Visual C++ compiler.
#ifdef _MSC_VER
#pragma warning (push, 0)
#endif

#include "external/stb_image.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm> // for std::clamp

class rtw_image {
public:
    rtw_image() = default;

    rtw_image(const char* image_filename) {
        std::string filename(image_filename);

        // Use a secure, MSVC-compatible way to read environment variables to avoid C4996
        std::string imagedir;
    #ifdef _MSC_VER
        char* env_buf = nullptr;
        size_t env_len = 0;
        if (_dupenv_s(&env_buf, &env_len, "RTW_IMAGES") == 0 && env_buf != nullptr) {
            imagedir = env_buf;
            free(env_buf);
        }
    #else
        const char* env_buf = std::getenv("RTW_IMAGES");
        if (env_buf) imagedir = env_buf;
    #endif

        if (!imagedir.empty() && load(imagedir + "/" + filename)) return;
        if (load(filename)) return;
        if (load("images/" + filename)) return;
        if (load("../images/" + filename)) return;
        if (load("../../images/" + filename)) return;
        if (load("../../../images/" + filename)) return;
        if (load("../../../../images/" + filename)) return;
        if (load("../../../../../images/" + filename)) return;
        if (load("../../../../../../images/" + filename)) return;

        std::cerr << "ERROR: Could not load image file '" << image_filename << "'.\n";
    }

    ~rtw_image() = default;

    rtw_image(const rtw_image&) = delete;
    rtw_image& operator=(const rtw_image&) = delete;
    rtw_image(rtw_image&&) = default;
    rtw_image& operator=(rtw_image&&) = default;

    bool load(const std::string& filename) {
        int n = bytes_per_pixel; 
        
        float* raw_fdata = stbi_loadf(filename.c_str(), &image_width, &image_height, &n, bytes_per_pixel);
        if (raw_fdata == nullptr) return false;

        fdata.reset(raw_fdata); 

        bytes_per_scanline = image_width * bytes_per_pixel;
        convert_to_bytes();
        return true;
    }

    int width()  const { return (fdata == nullptr) ? 0 : image_width; }
    int height() const { return (fdata == nullptr) ? 0 : image_height; }

    const unsigned char* pixel_data(int x, int y) const {
        static const unsigned char magenta[] = { 255, 0, 255 };
        if (bdata.empty()) return magenta;

        x = std::clamp(x, 0, image_width - 1);
        y = std::clamp(y, 0, image_height - 1);

        return bdata.data() + y * bytes_per_scanline + x * bytes_per_pixel;
    }

private:
    const int bytes_per_pixel = 3;
    
    struct stbi_deleter {
        void operator()(float* p) const { stbi_image_free(reinterpret_cast<void*>(p)); }
    };
    
    std::unique_ptr<float[], stbi_deleter> fdata = nullptr; 
    
    std::vector<unsigned char> bdata; 
    
    int image_width = 0;         
    int image_height = 0;        
    int bytes_per_scanline = 0;

    static unsigned char float_to_byte(float value) {
        if (value <= 0.0f) return 0;
        if (value >= 1.0f) return 255;
        return static_cast<unsigned char>(256.0f * value);
    }

    void convert_to_bytes() {
        int total_bytes = image_width * image_height * bytes_per_pixel;
        
        bdata.resize(total_bytes);

        auto* bptr = bdata.data();
        auto* fptr = fdata.get();
        for (int i = 0; i < total_bytes; i++, fptr++, bptr++) {
            *bptr = float_to_byte(*fptr);
        }
    }
};

// Restore MSVC compiler warnings
#ifdef _MSC_VER
#pragma warning (pop)
#endif