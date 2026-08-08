#include "Sync/VideoClock.h"

VideoClock::VideoClock()
{
    Reset();
}

void VideoClock::SetPts(
    double pts)
{
    std::lock_guard<std::mutex> lock(
        mutex);

    this->pts = pts;

    lastSet =
        std::chrono::steady_clock::now();

    initialized = true;
}

double VideoClock::Get() const
{
    std::lock_guard<std::mutex> lock(
        mutex);

    if (!initialized)
    {
        return pts;
    }

    auto now =
        std::chrono::steady_clock::now();

    double elapsed =
        std::chrono::duration<double>(
            now - lastSet)
            .count();

    return pts + elapsed;
}

void VideoClock::Reset()
{
    std::lock_guard<std::mutex> lock(
        mutex);

    pts = 0.0;

    initialized = false;

    lastSet =
        std::chrono::steady_clock::now();
}
