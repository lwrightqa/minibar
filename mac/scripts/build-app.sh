#!/usr/bin/env bash
#
# Builds MiniBar.app from the Swift package and signs it. macOS only.
#
#   scripts/build-app.sh                 # ad-hoc signed, this Mac's architecture
#   UNIVERSAL=1 scripts/build-app.sh     # Apple silicon and Intel (needs Xcode, not just the Command Line Tools)
#   SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" scripts/build-app.sh
#
# Settings (environment variables):
#   VERSION        CFBundleShortVersionString (default 1.0)
#   BUILD          CFBundleVersion (default: the number of git commits, else 1)
#   BUNDLE_ID      CFBundleIdentifier (default com.minibar.MiniBarMac; use your own
#                  reverse domain with a Developer ID)
#   SIGN_IDENTITY  "-" for ad-hoc signing (default), or a Developer ID Application identity
#   UNIVERSAL      1 to build for arm64 and x86_64
#   OUT_DIR        where MiniBar.app goes (default build/)
#   STABLE_ADHOC_REQUIREMENT
#                  1 to give an ad-hoc build the designated requirement
#                  'identifier "$BUNDLE_ID"' instead of its cdhash, so the
#                  Keychain keeps trusting rebuilds and doesn't ask "MiniBar
#                  wants to use your confidential information" after each one.
#                  Weaker: any ad-hoc app that claims this identifier would be
#                  trusted with MiniBar's tokens. Unverified on a Mac.
#
# The result is build/MiniBar.app. Copy it to /Applications before turning on
# Start at login (SMAppService wants the app there).

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "build-app.sh builds the macOS app and runs on macOS only." >&2
  echo "On Linux, build and test the core with: swift build && swift test" >&2
  exit 1
fi

cd "$(dirname "$0")/.."

VERSION="${VERSION:-1.0}"
BUILD="${BUILD:-$(git rev-list --count HEAD 2>/dev/null || echo 1)}"
BUNDLE_ID="${BUNDLE_ID:-com.minibar.MiniBarMac}"
SIGN_IDENTITY="${SIGN_IDENTITY:--}"
OUT_DIR="${OUT_DIR:-build}"
APP="$OUT_DIR/MiniBar.app"

ARCH_FLAGS=()
if [[ "${UNIVERSAL:-0}" == "1" ]]; then
  ARCH_FLAGS=(--arch arm64 --arch x86_64)
fi

echo "Building MiniBar $VERSION ($BUILD), release…"
swift build -c release --product MiniBar ${ARCH_FLAGS[@]+"${ARCH_FLAGS[@]}"}
BIN_DIR="$(swift build -c release --product MiniBar ${ARCH_FLAGS[@]+"${ARCH_FLAGS[@]}"} --show-bin-path)"

echo "Assembling $APP…"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_DIR/MiniBar" "$APP/Contents/MacOS/MiniBar"
sed -e "s/__VERSION__/$VERSION/g" \
    -e "s/__BUILD__/$BUILD/g" \
    -e "s/__BUNDLE_ID__/$BUNDLE_ID/g" \
    Resources/Info.plist > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
if [[ -f Resources/AppIcon.icns ]]; then
  cp Resources/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
  /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string AppIcon" "$APP/Contents/Info.plist"
fi
plutil -lint "$APP/Contents/Info.plist" >/dev/null

# Never ship a microphone or camera usage description (criterion 30).
if /usr/libexec/PlistBuddy -c "Print :NSMicrophoneUsageDescription" "$APP/Contents/Info.plist" >/dev/null 2>&1 ||
   /usr/libexec/PlistBuddy -c "Print :NSCameraUsageDescription" "$APP/Contents/Info.plist" >/dev/null 2>&1; then
  echo "Info.plist must not ask for the microphone or camera." >&2
  exit 1
fi

echo "Signing with ${SIGN_IDENTITY/#-/an ad-hoc signature}…"
if [[ "$SIGN_IDENTITY" == "-" ]]; then
  # Ad hoc: no Team ID, so IT can only allow it by hash, and the hash changes
  # with every build (README, For IT).
  if [[ "${STABLE_ADHOC_REQUIREMENT:-0}" == "1" ]]; then
    codesign --force --sign - --timestamp=none \
      --requirements "=designated => identifier \"$BUNDLE_ID\"" "$APP"
  else
    codesign --force --sign - --timestamp=none "$APP"
  fi
else
  # Developer ID: hardened runtime, no entitlements (no audio-input or camera).
  codesign --force --sign "$SIGN_IDENTITY" --options runtime --timestamp "$APP"
fi
codesign --verify --strict --verbose=2 "$APP"

echo "Built $APP"
if [[ "$SIGN_IDENTITY" != "-" ]]; then
  echo "To notarize: ditto -c -k --keepParent \"$APP\" MiniBar.zip"
  echo "  xcrun notarytool submit MiniBar.zip --keychain-profile <profile> --wait"
  echo "  xcrun stapler staple \"$APP\""
fi
