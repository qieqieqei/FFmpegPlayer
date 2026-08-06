#include "Screenshot.h"

#include <fstream>
#include <iostream>
#include <filesystem>     // C++17 文件夹操作
#include <sstream>
#include <iomanip>



std::string GetUniqueFilename(
    const std::string& filename)
{
    namespace fs = std::filesystem;


    std::string result =
        filename;      // 初始文件名

    int index = 1;


    while (fs::exists(
        "screenshots/" + result))
    {

        std::ostringstream oss;


        oss
            << filename.substr(
                0,
                filename.find(".bmp"))
            << "_"
            << std::setw(3)
            << std::setfill('0')
            << index++
            << ".bmp";


        result =
            oss.str();
    }


    return result;
}


bool SaveScreenshotBMP(
    const uint8_t* rgbData,
    int width,
    int height,
    int linesize,
    const std::string& filename)
{

    std::filesystem::create_directories(
        "screenshots");       // 自动创建截图目录

    if (!rgbData)
    {
        return false;       // RGB数据为空
    }

    std::string uniqueName =
        GetUniqueFilename(
            filename);

    std::string path =
        "screenshots/" + filename;      // 保存到screenshots目录


    std::ofstream file(
        path,
        std::ios::binary);

    


    if (!file)
    {
        return false;       // 文件创建失败
    }



    int rowSize =
        (width * 3 + 3) & ~3;      // BMP每行4字节对齐



    int imageSize =
        rowSize * height;          // 图片总大小



    int fileSize =
        54 + imageSize;            // 文件头54字节



    unsigned char header[54] = { 0 };



    header[0] = 'B';               // BMP标识
    header[1] = 'M';



    *(int*)&header[2] =
        fileSize;                  // 文件大小



    *(int*)&header[10] =
        54;                        // 数据偏移



    *(int*)&header[14] =
        40;                        // 信息头大小



    *(int*)&header[18] =
        width;                     // 宽度



    *(int*)&header[22] =
        -height;                   // 高度，负数表示顶部开始



    *(short*)&header[26] =
        1;                         // 平面数



    *(short*)&header[28] =
        24;                        // RGB24



    file.write(
        (char*)header,
        54);



    uint8_t* rowBuffer =
        new uint8_t[rowSize];


    for (int y = 0; y < height; y++)
    {

        memcpy(
            rowBuffer,
            rgbData + y * linesize,
            width * 3);


        file.write(
            (char*)rowBuffer,
            rowSize);
    }


    delete[] rowBuffer;



    file.close();



    std::cout
        << "Screenshot Saved : "
        << filename
        << std::endl;


    return true;
}



