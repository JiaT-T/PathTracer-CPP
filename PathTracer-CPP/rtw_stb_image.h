#pragma once

// 1. 解决 MSVC 的 getenv 警告：告诉编译器我们知道自己在干嘛
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

    // 推荐使用 std::string_view (C++17) 替代 const char*
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

    // 2. 现代 C++ 信仰 (RAII): 不再需要手写析构函数！
    // 智能指针和 std::vector 会自动、安全地清理内存。
    ~rtw_image() = default;

    // 禁用拷贝，防止意外的性能开销，但允许移动 (Move)
    rtw_image(const rtw_image&) = delete;
    rtw_image& operator=(const rtw_image&) = delete;
    rtw_image(rtw_image&&) = default;
    rtw_image& operator=(rtw_image&&) = default;

    bool load(const std::string& filename) {
        int n = bytes_per_pixel; 
        
        // 3. 使用 std::unique_ptr 管理 stb_image 分配的 C 风格内存
        // 这样即使多次调用 load，旧内存也会自动被 stbi_image_free 释放，杜绝内存泄漏
        float* raw_fdata = stbi_loadf(filename.c_str(), &image_width, &image_height, &n, bytes_per_pixel);
        if (raw_fdata == nullptr) return false;

        // 将裸指针交由智能指针接管，并指定释放函数
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

        // 使用 std::clamp (C++17)，代替手写的 clamp 函数
        x = std::clamp(x, 0, image_width - 1);
        y = std::clamp(y, 0, image_height - 1);

        // vector 在内存中保证是连续的，可以通过 .data() 获取底层指针
        return bdata.data() + y * bytes_per_scanline + x * bytes_per_pixel;
    }

private:
    const int bytes_per_pixel = 3;
    
    // 4. 定义一个自定义删除器，告诉 unique_ptr 如何释放 stb 内存
    struct stbi_deleter {
        void operator()(float* p) const { stbi_image_free(reinterpret_cast<void*>(p)); }
    };
    
    std::unique_ptr<float[], stbi_deleter> fdata = nullptr; 
    
    // 5. 使用 std::vector 替代裸指针 new unsigned char[]
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
        
        // vector 自动管理内存扩容
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