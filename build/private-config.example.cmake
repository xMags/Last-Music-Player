# Copy to build/private-config.local.cmake (ignored by git) and fill in.
# Leave a value empty to keep the feature that needs it switched off.

# Account service origins: scheme and host only, https, no trailing slash.
set(LMP_ACCOUNT_API_ORIGIN "")
set(LMP_ACCOUNT_FRONTEND_ORIGIN "")
set(LMP_ACCOUNT_MEDIA_ORIGIN "")

# Application id used for the Discord presence integration.
set(LMP_DISCORD_CLIENT_ID "")
