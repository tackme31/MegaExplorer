#include "BusyState.h"

namespace
{

// How long an operation has to stay in flight before the tab shows a spinner.
// Every operation the busy count covers is a server round-trip, so most land
// well inside this and never flash one; the ones that don't are the ones
// worth reporting.
constexpr int kBusyIndicatorDelayMs = 250;

} // namespace

BusyState::BusyState(QObject* parent) : QObject(parent)
{
    mDelayTimer.setSingleShot(true);
    mDelayTimer.setInterval(kBusyIndicatorDelayMs);
    connect(&mDelayTimer, &QTimer::timeout, this, [this]() {
        // The timer is stopped by end(), so this normally can't fire with
        // nothing left in flight -- guarded anyway rather than relying on that
        // ordering.
        if (mCount == 0 || mVisible)
            return;
        mVisible = true;
        emit changed();
    });
}

bool BusyState::visible() const
{
    return mVisible;
}

void BusyState::begin()
{
    if (++mCount == 1)
        mDelayTimer.start();
}

void BusyState::end()
{
    // abandonAll() zeroes the count with operations still in flight, so their
    // callbacks arrive here with nothing left to subtract. Clamping rather
    // than letting the count go negative, which would stop a later begin()
    // from ever reaching 1 again.
    if (mCount == 0)
        return;

    if (--mCount > 0)
        return;

    mDelayTimer.stop();
    if (!mVisible)
        return;
    mVisible = false;
    emit changed();
}

void BusyState::abandonAll()
{
    mCount = 0;
    mDelayTimer.stop();
    if (!mVisible)
        return;
    mVisible = false;
    emit changed();
}
