#include "Playlist/PlaylistManager.h"
#include "Utils/Logger.h"

#include <iostream>
#include "Utils/Logger.h"

PlaylistManager::PlaylistManager()
{
}

void PlaylistManager::AddMedia(
    const std::string& path)
{
    items.push_back(path);

    Logger::Info()
        << "[Playlist] Add : "
        << path
        << " ("
        << items.size()
        << " items)"
        << std::endl;
}

void PlaylistManager::Clear()
{
    items.clear();

    index = 0;
}

bool PlaylistManager::Next()
{
    if (items.empty())
    {
        return false;
    }

    index =
        (index + 1) % items.size();

    return true;
}

bool PlaylistManager::Previous()
{
    if (items.empty())
    {
        return false;
    }

    // 回到上一首（取模避免负数）
    index =
        (index + items.size() - 1) %
        items.size();

    return true;
}

bool PlaylistManager::HasNext() const
{
    return !items.empty();
}

bool PlaylistManager::HasPrevious() const
{
    return !items.empty();
}

const std::string& PlaylistManager::GetCurrent() const
{
    if (items.empty())
    {
        // 返回一个静态空串，避免悬垂引用
        static const std::string empty;

        return empty;
    }

    return items[index];
}

void PlaylistManager::SetIndex(
    size_t index)
{
    if (items.empty())
    {
        this->index = 0;

        return;
    }

    this->index =
        index % items.size();
}

size_t PlaylistManager::GetIndex() const
{
    return index;
}

size_t PlaylistManager::Count() const
{
    return items.size();
}

void PlaylistManager::SetAutoPlay(
    bool on)
{
    autoPlay = on;
}

bool PlaylistManager::IsAutoPlay() const
{
    return autoPlay;
}
