
#pragma once

#include <string>
#include <cstdint>


bool SaveScreenshotBMP(
    const uint8_t* rgbData,     // RGB数据首地址
    int width,                  // 图片宽
    int height,                 // 图片高
    int linesize,               // 每行字节数
    const std::string& filename);