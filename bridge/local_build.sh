#!/usr/bin/env bash
# Локальная сборка APK на ПК (WSL2 Ubuntu 24.04) — копия шагов .github/workflows/release-android.yml.
# Запуск внутри WSL:  bash bridge/local_build.sh            (первый раз: долго, vcpkg ~1-2 ч)
#                     bash bridge/local_build.sh --fast     (повторно: только пересборка)
# Итог: ~/GeneralsZH-Android.apk  и копия в /mnt/c/Users/$WINUSER/GeneralsZH-Android.apk
set -euo pipefail
WINUSER=${WINUSER:-lbx95}
NDK_VERSION=27.2.12479018
SDK=$HOME/android-sdk
REPO=$HOME/SandboxRTS-Android
FAST=0; [[ "${1:-}" == "--fast" ]] && FAST=1
log(){ echo "=== $* ==="; }

if [[ $FAST -eq 0 ]]; then
  log "apt"
  sudo apt-get update
  sudo apt-get install -y git curl unzip zip tar pkg-config build-essential cmake ninja-build meson glslang-tools nasm ccache python3 openjdk-17-jdk autoconf automake libtool bison flex
  ccache -M 5G || true

  if [[ ! -x $SDK/cmdline-tools/latest/bin/sdkmanager ]]; then
    log "android cmdline-tools"
    mkdir -p $SDK/cmdline-tools && cd /tmp
    curl -fsSL -o clt.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
    rm -rf cmdline-tools && unzip -q clt.zip && rm -rf $SDK/cmdline-tools/latest && mv cmdline-tools $SDK/cmdline-tools/latest
  fi
  log "NDK + platforms"
  yes | $SDK/cmdline-tools/latest/bin/sdkmanager --sdk_root=$SDK --licenses >/dev/null || true
  $SDK/cmdline-tools/latest/bin/sdkmanager --sdk_root=$SDK "ndk;$NDK_VERSION" 'platforms;android-34' 'build-tools;34.0.0' 'platform-tools' >/dev/null

  if [[ ! -x /opt/gradle/gradle-8.9/bin/gradle ]]; then
    log "gradle 8.9"
    curl -fsSL -o /tmp/gradle.zip https://services.gradle.org/distributions/gradle-8.9-bin.zip
    sudo unzip -q -o /tmp/gradle.zip -d /opt/gradle
  fi

  if [[ ! -x $HOME/vcpkg/vcpkg ]]; then
    log "vcpkg"
    git clone https://github.com/microsoft/vcpkg.git $HOME/vcpkg
    $HOME/vcpkg/bootstrap-vcpkg.sh -disableMetrics
  fi
fi

export ANDROID_SDK_ROOT=$SDK ANDROID_HOME=$SDK
export ANDROID_NDK_HOME=$SDK/ndk/$NDK_VERSION
export VCPKG_ROOT=$HOME/vcpkg
export PATH=/opt/gradle/gradle-8.9/bin:$SDK/platform-tools:$PATH
export JAVA_HOME=$(dirname $(dirname $(readlink -f $(which javac))))

[[ -d $REPO/.git ]] || { echo "Нет $REPO — склонируй репо (см. LOCAL_BUILD.md)"; exit 1; }
cd $REPO
log "git: main"
git fetch origin main && git checkout -q main && git reset -q --hard origin/main
git submodule update --init --recursive references/libadrenotools
git submodule update --init references/fadi-labib-dxvk
echo "commit $(git rev-parse --short HEAD)"

log "turnip + adrenotools"
./scripts/build/android/fetch-turnip.sh
./scripts/build/android/build-adrenotools.sh
log "cmake configure"
cmake --preset android-vulkan
log "build"
J=$(( $(nproc) > 2 ? $(nproc) - 1 : 1 ))
cmake --build build/android-vulkan --target z_generals dxvk_d3d8_install -j$J
log "package"
./scripts/build/android/package-android-zh.sh
cp android/app/build/outputs/apk/debug/app-debug.apk ~/GeneralsZH-Android.apk
cp ~/GeneralsZH-Android.apk /mnt/c/Users/$WINUSER/GeneralsZH-Android.apk 2>/dev/null || true
ls -l ~/GeneralsZH-Android.apk
echo "BUILD_OK $(git rev-parse --short HEAD)"
