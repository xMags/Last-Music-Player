#pragma once

#include "Backend/AccountArtistDetailPolicy.h"
#include "Backend/RemoteDetailContract.h"
#include "Backend/TrackInfo.h"
#include "Frontend/ArtistPagePolicy.h"

namespace LastMusicPlayer::Frontend
{
    using ArtistNavigationEntry = ArtistNavigationEntryState<
        LastMusicPlayer::Backend::RemoteArtistDetail,
        LastMusicPlayer::Backend::RemoteDetailIdentity,
        LastMusicPlayer::Backend::AccountArtistDetailPlan,
        winrt::Last_Music_Player::TrackInfo>;

    using ArtistNavigationSnapshot = ArtistNavigationSnapshotState<ArtistNavigationEntry>;
}
