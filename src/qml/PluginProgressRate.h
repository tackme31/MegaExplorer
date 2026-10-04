#pragma once
#include <QtGlobal>

#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

// Items per second over a plugin run's last few increases of ui.progress's current,
// so the figure and the time-left estimate follow a run that speeds up or slows down.
class PluginProgressRate
{
public:
    void add(qint64 atMs, qint64 current)
    {
        // A count that goes back is a new phase of the run: its old pace says nothing.
        if (current < 0 || (!mSamples.empty() && current < mSamples.back().second))
            mSamples.clear();
        if (current < 0 || (!mSamples.empty() && current == mSamples.back().second))
            return;
        mSamples.emplace_back(atMs, current);
        while (mSamples.size() > kWindow + 1)
            mSamples.pop_front();
    }

    // nullopt until two increases lie apart in time.
    std::optional<double> perSecond() const
    {
        if (mSamples.size() < 2)
            return std::nullopt;
        const qint64 elapsedMs = mSamples.back().first - mSamples.front().first;
        if (elapsedMs <= 0)
            return std::nullopt;
        return static_cast<double>(mSamples.back().second - mSamples.front().second) * 1000.0 /
               static_cast<double>(elapsedMs);
    }

    // When the newest increase arrived, so time left can count down between reports.
    qint64 lastIncreaseAt() const { return mSamples.empty() ? 0 : mSamples.back().first; }

private:
    static constexpr std::size_t kWindow = 10;
    std::deque<std::pair<qint64, qint64>> mSamples;
};
