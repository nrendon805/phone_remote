# Gasoline Remote (Android, standalone APK)

This is your `phone_remote.cpp` SDL2 app, wrapped as a real Android Gradle
project. You don't need Android Studio or the NDK installed anywhere — a
GitHub Actions workflow in this repo (`.github/workflows/build-apk.yml`)
builds the APK automatically whenever this project is pushed to GitHub.

## One-time setup

1. Create a new (empty) repository on GitHub, e.g. `GasolineRemote`.
2. From this folder, run:
   ```
   git init
   git add -A
   git commit -m "Initial Android project"
   git branch -M main
   git remote add origin https://github.com/<your-username>/GasolineRemote.git
   git push -u origin main
   ```
3. On GitHub, open the repo's **Actions** tab. A "Build APK" run should
   already be in progress (it triggers automatically on push).
4. When it finishes (green check, a few minutes), click into the run,
   scroll to **Artifacts**, and download `GasolineRemote-debug-apk`. Unzip
   it — that's your `app-debug.apk`.
5. Copy the `.apk` to your phone (email, USB, cloud drive, whatever) and
   tap it to install. Android will ask you to allow "install unknown apps"
   for whichever app you used to open it — allow that once.

## Making changes later

Every time you want to update the app:
- Edit `app/jni/src/phone_remote.cpp` (that's your source file, unchanged
  logic from the Cxxdroid version plus the window/renderer crash fixes).
- `git add -A && git commit -m "..." && git push`
- Wait for Actions to finish, download the new APK from the same place.

## What's in here

- `app/jni/SDL/` — the SDL2 library source (pulled from the official
  `libsdl-org/SDL` repo), built directly alongside your code via ndk-build.
  You never need to touch this.
- `app/jni/src/phone_remote.cpp` — your app's actual code.
- `app/jni/src/Android.mk` — tells ndk-build to compile `phone_remote.cpp`
  into `libmain.so`, linked against SDL2. This is what Android's
  `SDLActivity` loads and calls `main()` in.
- `app/src/main/AndroidManifest.xml` — app permissions (added `INTERNET`
  and `ACCESS_NETWORK_STATE` so it can reach the PC soundboard over WiFi)
  and the `SDLActivity` launcher activity.
- `.github/workflows/build-apk.yml` — the GitHub Actions workflow that
  installs the Android NDK/build-tools and runs `./gradlew assembleDebug`
  on every push, then uploads the resulting APK as a downloadable artifact.

## Notes

- This builds a **debug** APK (unsigned release builds need a signing key,
  debug APKs are self-signed automatically and install fine on your own
  phone — just not publishable to the Play Store as-is).
- `SERVER_IP` in `phone_remote.cpp` still needs to match your PC's LAN IP,
  same as before.
