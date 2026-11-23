#pragma once

#include "core/model/Track.h"

namespace lmp::core {

// Where a qualifying play of an account song goes instead of the local
// history, in Account mode. Called on the player's thread.
class AccountPlaybackSink {
public:
    virtual ~AccountPlaybackSink() = default;

    // True when the play belongs to the account and was handled there; the
    // local history is then not written.
    virtual bool recordAccountPlay(const Track& track, double positionSeconds) = 0;

protected:
    AccountPlaybackSink() = default;
    AccountPlaybackSink(const AccountPlaybackSink&) = default;
    AccountPlaybackSink& operator=(const AccountPlaybackSink&) = default;
};

} // namespace lmp::core
