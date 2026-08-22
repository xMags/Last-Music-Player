#pragma once

// Build-private configuration template.
//
// Copy this file to "src/Backend/AppSecrets.local.h" (which is gitignored) and fill
// in the values to enable build-private integrations in your own builds. The
// app compiles and runs fine without it — these are entirely optional.
//
//   copy src\Backend\AppSecrets.example.h src\Backend\AppSecrets.local.h
//
// Currently used:
//
//   LMP_DISCORD_CLIENT_ID  Your Discord application's client id, enabling
//                          Discord Rich Presence ("Listening to ..."). Create
//                          an app at https://discord.com/developers/applications
//                          and paste its Application ID here. When this is left
//                          undefined (the public default), Rich Presence is
//                          simply disabled.

// #define LMP_DISCORD_CLIENT_ID "000000000000000000"
