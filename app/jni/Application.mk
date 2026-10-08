
# phone_remote.cpp uses std::string/std::vector/exceptions, which need the
# C++ standard library linked in (otherwise ld fails with "undefined symbol:
# std::__ndk1::..." for things like std::string's copy constructor).
APP_STL := c++_shared

APP_ABI := armeabi-v7a arm64-v8a x86 x86_64

# Min runtime API level
APP_PLATFORM=android-16
