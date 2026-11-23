# Copy to build/private-config.local.cmake (ignored by git) and fill in.
# Leave a value empty to keep the feature that needs it switched off.

# Account service origins: scheme and host only, https, no trailing slash.
set(LMP_ACCOUNT_API_ORIGIN "")
set(LMP_ACCOUNT_FRONTEND_ORIGIN "")
set(LMP_ACCOUNT_MEDIA_ORIGIN "")

# Application id used for the Discord presence integration.
set(LMP_DISCORD_CLIENT_ID "")

# Source tag of catalogue items that carry the catalogue's own numeric ids.
set(LMP_SOURCE_TAG_CATALOG "")

# Comma-separated source tags whose songs and albums the service resolves
# directly; the first is assumed when an item has none.
set(LMP_SOURCE_TAGS_STREAM "")

# Comma-separated words (lower case) that name where music comes from; they
# are never shown as an artist, curator or album name.
set(LMP_SOURCE_LABELS "")

# Prefix that marks an external playlist id.
set(LMP_EXTERNAL_PLAYLIST_ID_PREFIX "")
