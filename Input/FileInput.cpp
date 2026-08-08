#include "Input/FileInput.h"

#include "Config/ConfigManager.h"
#include "Utils/Logger.h"

// ============================================================
// FileInput - 本地文件输入
// ============================================================

FileInput::FileInput()
{
    protocol = "file";
}

FileInput::~FileInput()
{
    Close();
}

bool FileInput::Open(
    const std::string& url)
{
    // 剥离 file:// 前缀
    std::string path =
        ConfigManager::StripFileScheme(url);

    Logger::Info()
        << "[FileInput] Open : "
        << path
        << std::endl;

    // 本地文件：无特殊选项
    if (!OpenWithOptions(
        path,
        nullptr))
    {
        return false;
    }

    return true;
}

void FileInput::Close()
{
    // RAII：AVFormatContextPtr 自动 avformat_close_input
    fmt.reset();
}
