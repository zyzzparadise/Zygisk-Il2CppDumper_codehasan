//
// Created by Perfare on 2020/7/4.
//

#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>
#include <cerrno>
#include <cstdint>


#if defined(__aarch64__)
static char g_tls_probe_status_path[1024] = {};

static void TlsProbeStatus(const char *stage, const char *detail = "") {
    LOGI("TLS_PROBE: %s %s", stage, detail);
    if (!g_tls_probe_status_path[0]) return;
    FILE *f = fopen(g_tls_probe_status_path, "a");
    if (!f) {
        LOGE("TLS_PROBE: cannot write status file (errno=%d)", errno);
        return;
    }
    fprintf(f, "pid=%ld  %s  %s\n",
            static_cast<long>(getpid()), stage, detail);
    fclose(f);
}
#endif

// Diagnostic, opt-in TLS trust probe for the exact Soul Knight ARM64 build
// captured in SoulKnight-TLS-disasm.txt. Disabled by default.
// It touches only ChillyRoom.RPC.RPCClient's two leaf validation methods;
// RGScript.Util.SslUtil remains unchanged.
#if defined(__aarch64__)
static int MemoryProtectionFor(uintptr_t addr) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        LOGE("TLS_PROBE: cannot open /proc/self/maps (errno=%d)", errno);
        return -1;
    }

    char line[1024];
    int protection = -1;
    while (fgets(line, sizeof(line), maps)) {
        unsigned long long begin = 0, end = 0;
        char perms[5] = {};
        if (sscanf(line, "%llx-%llx %4s", &begin, &end, perms) != 3) continue;
        if (addr < begin || addr >= end) continue;
        protection = 0;
        if (perms[0] == 'r') protection |= PROT_READ;
        if (perms[1] == 'w') protection |= PROT_WRITE;
        if (perms[2] == 'x') protection |= PROT_EXEC;
        break;
    }
    fclose(maps);
    return protection;
}

static void MaybeEnableSoulKnightTlsProbe(void *handle) {
    TlsProbeStatus("entered opt-in TLS probe");
    char setting[PROP_VALUE_MAX] = {};
    const int settingLength = __system_property_get("persist.il2cppdumper.tls_probe", setting);
    char propertyInfo[160] = {};
    snprintf(propertyInfo, sizeof(propertyInfo), "read_count=%d, value='%s'",
             settingLength, setting);
    TlsProbeStatus("flag", propertyInfo);
    if (strcmp(setting, "1") != 0) {
        TlsProbeStatus("disabled: ARM64 property is not 1");
        return;
    }

    char package[PROP_VALUE_MAX] = {};
    const int packageLength = __system_property_get("persist.il2cppdumper.package", package);
    snprintf(propertyInfo, sizeof(propertyInfo), "read_count=%d, value='%s'",
             packageLength, package);
    TlsProbeStatus("package", propertyInfo);
    if (strcmp(package, "com.ChillyRoom.DungeonShooter") != 0) {
        TlsProbeStatus("blocked: ARM64 package property mismatch");
        LOGE("TLS_PROBE: blocked (target package property mismatch: %s)", package);
        return;
    }

    xdl_info_t info{};
    if (xdl_info(handle, XDL_DI_DLINFO, &info) != 0 || !info.dli_fbase) {
        TlsProbeStatus("blocked: xdl_info failed");
        LOGE("TLS_PROBE: xdl_info failed");
        return;
    }
    TlsProbeStatus("library", info.dli_fname ? info.dli_fname : "(unknown)");
    if (!info.dli_fname || !strstr(info.dli_fname, "com.ChillyRoom.DungeonShooter")) {
        TlsProbeStatus("blocked: wrong library path");
        LOGE("TLS_PROBE: unexpected library path: %s",
             info.dli_fname ? info.dli_fname : "(null)");
        return;
    }

    // ELF virtual addresses from the ARM64 dump, NOT on-disk offsets.
    static constexpr uintptr_t kErrorCodesRva = 0x0991f9f8;
    static constexpr uintptr_t kHashTrustedRva = 0x0991fc98;
    // Check 20 bytes to avoid silently changing another game/IL2CPP build.
    static constexpr unsigned char kErrorCodesStart[] = {
        0xfe, 0x6f, 0xbb, 0xa9, 0xfa, 0x67, 0x01, 0xa9,
        0xf8, 0x5f, 0x02, 0xa9, 0xf6, 0x57, 0x03, 0xa9,
        0xf4, 0x4f, 0x04, 0xa9
    };
    static constexpr unsigned char kHashTrustedStart[] = {
        0xfe, 0x0f, 0x1b, 0xf8, 0xfa, 0x67, 0x01, 0xa9,
        0xf8, 0x5f, 0x02, 0xa9, 0xf6, 0x57, 0x03, 0xa9,
        0xf4, 0x4f, 0x04, 0xa9
    };
    // AArch64: mov w0, #1 ; ret. No stack adjustment.
    static constexpr unsigned char kReturnTrue[] = {
        0x20, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6
    };

    const uintptr_t bias = reinterpret_cast<uintptr_t>(info.dli_fbase);
    const uintptr_t errorAddress = bias + kErrorCodesRva;
    const uintptr_t hashAddress = bias + kHashTrustedRva;
    LOGI("TLS_PROBE: lib base=%p error=%p hash=%p",
         info.dli_fbase, reinterpret_cast<void *>(errorAddress),
         reinterpret_cast<void *>(hashAddress));
    TlsProbeStatus("resolved target addresses");

    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0 || (pageSize & (pageSize - 1)) != 0) {
        TlsProbeStatus("blocked: invalid page size");
        LOGE("TLS_PROBE: invalid page size: %ld", pageSize);
        return;
    }
    const uintptr_t mask = static_cast<uintptr_t>(pageSize - 1);
    const uintptr_t errorPage = errorAddress & ~mask;
    const uintptr_t hashPage = hashAddress & ~mask;
    if (errorPage != hashPage) {
        TlsProbeStatus("blocked: target addresses on different pages");
        LOGE("TLS_PROBE: targets on different pages (unsupported)");
        return;
    }

    const int originalProtection = MemoryProtectionFor(errorAddress);
    if (originalProtection < 0 ||
        MemoryProtectionFor(hashAddress) != originalProtection) {
        TlsProbeStatus("blocked: map permission check failed");
        LOGE("TLS_PROBE: map permission lookup failed or mismatched");
        return;
    }
    if (!(originalProtection & PROT_READ)) {
        TlsProbeStatus("blocked: mapping not readable");
        LOGE("TLS_PROBE: code mapping is not readable");
        return;
    }
    if (memcmp(reinterpret_cast<const void *>(errorAddress),
               kErrorCodesStart, sizeof(kErrorCodesStart)) != 0 ||
        memcmp(reinterpret_cast<const void *>(hashAddress),
               kHashTrustedStart, sizeof(kHashTrustedStart)) != 0) {
        TlsProbeStatus("blocked: build signature mismatch, no changes");
        LOGE("TLS_PROBE: build mismatch, byte signatures rejected. No changes.");
        return;
    }

    void *page = reinterpret_cast<void *>(errorPage);
    if (mprotect(page, static_cast<size_t>(pageSize), PROT_READ | PROT_WRITE) != 0) {
        TlsProbeStatus("blocked: mprotect RW failed");
        LOGE("TLS_PROBE: mprotect RW failed (errno=%d)", errno);
        return;
    }

    memcpy(reinterpret_cast<void *>(errorAddress), kReturnTrue, sizeof(kReturnTrue));
    memcpy(reinterpret_cast<void *>(hashAddress), kReturnTrue, sizeof(kReturnTrue));
    __builtin___clear_cache(reinterpret_cast<char *>(errorAddress),
                            reinterpret_cast<char *>(errorAddress + sizeof(kReturnTrue)));
    __builtin___clear_cache(reinterpret_cast<char *>(hashAddress),
                            reinterpret_cast<char *>(hashAddress + sizeof(kReturnTrue)));

    if (mprotect(page, static_cast<size_t>(pageSize), originalProtection) != 0) {
        TlsProbeStatus("WARNING: cannot restore original page protections");
        LOGE("TLS_PROBE: WARNING: cannot restore original protections (errno=%d)", errno);
        return;
    }
    TlsProbeStatus("applied two RPCClient trust-check overrides (in memory)");
    LOGI("TLS_PROBE: applied two RPCClient trust-check overrides (in memory)");
}
#endif

void hack_start(const char *game_data_dir) {
#if defined(__aarch64__)
    snprintf(g_tls_probe_status_path, sizeof(g_tls_probe_status_path),
             "%s/files/tls-probe-status.txt", game_data_dir);
    FILE *initialStatus = fopen(g_tls_probe_status_path, "w");
    if (initialStatus) {
        fprintf(initialStatus, "ARM64 component entered hack_start, pid=%ld\n",
                static_cast<long>(getpid()));
        fclose(initialStatus);
    } else {
        LOGE("TLS_PROBE: initial status file failed, errno=%d", errno);
    }
    TlsProbeStatus("ARM64 hack_start started");
#endif
    bool load = false;
    for (int i = 0; i < 10; i++) {
        void *handle = xdl_open("libil2cpp.so", 0);
        if (handle) {
            load = true;
#if defined(__aarch64__)
            TlsProbeStatus("libil2cpp.so found");
            MaybeEnableSoulKnightTlsProbe(handle);
#endif
            il2cpp_api_init(handle);
            il2cpp_dump(game_data_dir);
            break;
        } else {
            sleep(1);
        }
    }
    if (!load) {
#if defined(__aarch64__)
        TlsProbeStatus("ERROR: libil2cpp.so not found");
#endif
        LOGI("libil2cpp.so not found in thread %d", gettid());
    }
}

std::string GetLibDir(JavaVM *vms) {
    JNIEnv *env = nullptr;
    vms->AttachCurrentThread(&env, nullptr);
    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
    if (activity_thread_clz != nullptr) {
        jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
                                                                "currentApplication",
                                                                "()Landroid/app/Application;");
        if (currentApplicationId) {
            jobject application = env->CallStaticObjectMethod(activity_thread_clz,
                                                              currentApplicationId);
            if (application == nullptr) {
                LOGE("currentApplication returned null");
                return {};
            }
            jclass application_clazz = env->GetObjectClass(application);
            if (application_clazz) {
                jmethodID get_application_info = env->GetMethodID(application_clazz,
                                                                  "getApplicationInfo",
                                                                  "()Landroid/content/pm/ApplicationInfo;");
                if (get_application_info) {
                    jobject application_info = env->CallObjectMethod(application,
                                                                     get_application_info);
                    jfieldID native_library_dir_id = env->GetFieldID(
                            env->GetObjectClass(application_info), "nativeLibraryDir",
                            "Ljava/lang/String;");
                    if (native_library_dir_id) {
                        auto native_library_dir_jstring = (jstring) env->GetObjectField(
                                application_info, native_library_dir_id);
                        auto path = env->GetStringUTFChars(native_library_dir_jstring, nullptr);
                        LOGI("lib dir %s", path);
                        std::string lib_dir(path);
                        env->ReleaseStringUTFChars(native_library_dir_jstring, path);
                        return lib_dir;
                    } else {
                        LOGE("nativeLibraryDir not found");
                    }
                } else {
                    LOGE("getApplicationInfo not found");
                }
            } else {
                LOGE("application class not found");
            }
        } else {
            LOGE("currentApplication not found");
        }
    } else {
        LOGE("ActivityThread not found");
    }
    return {};
}

static std::string GetNativeBridgeLibrary() {
    auto value = std::array<char, PROP_VALUE_MAX>();
    __system_property_get("ro.dalvik.vm.native.bridge", value.data());
    return {value.data()};
}

struct NativeBridgeCallbacks {
    uint32_t version;
    void *initialize;

    void *(*loadLibrary)(const char *libpath, int flag);

    void *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);

    void *isSupported;
    void *getAppEnv;
    void *isCompatibleWith;
    void *getSignalHandler;
    void *unloadLibrary;
    void *getError;
    void *isPathSupported;
    void *initAnonymousNamespace;
    void *createNamespace;
    void *linkNamespaces;

    void *(*loadLibraryExt)(const char *libpath, int flag, void *ns);
};

// Wait for the Android application using the JavaVM supplied by Zygisk.
// No dlopen("libart.so") or JNI_GetCreatedJavaVMs is required.
static bool WaitForRuntime(JavaVM *vm) {
    if (!vm) {
        LOGE("WaitForRuntime: JavaVM is null");
        return false;
    }

    JNIEnv *env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
        LOGE("AttachCurrentThread failed");
        return false;
    }

    for (int i = 0; i < 300; ++i) {
        bool ready = false;

        jclass cls = env->FindClass("android/app/ActivityThread");
        if (cls) {
            jmethodID method = env->GetStaticMethodID(
                    cls,
                    "currentApplication",
                    "()Landroid/app/Application;");

            if (method) {
                jobject app = env->CallStaticObjectMethod(cls, method);
                if (app && !env->ExceptionCheck()) {
                    ready = true;
                }
                if (app) {
                    env->DeleteLocalRef(app);
                }
            }
            env->DeleteLocalRef(cls);
        }

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        if (ready) {
            LOGI("JavaVM and Application ready");
            return true;
        }

        usleep(100 * 1000);
    }

    LOGE("Timed out waiting for Application");
    return false;
}

bool NativeBridgeLoad(
        const char *game_data_dir,
        int api_level,
        void *data,
        size_t length,
        JavaVM *vm) {
    JavaVM *vms = vm;
    if (!WaitForRuntime(vms)) {
        LOGE("runtime/application not ready");
        return false;
    }
    auto lib_dir = GetLibDir(vms);
    if (lib_dir.empty()) {
        LOGE("GetLibDir error");
        return false;
    }
    if (lib_dir.find("/lib/x86") != std::string::npos) {
        LOGI("no need NativeBridge");
        munmap(data, length);
        return false;
    }

    auto nb = dlopen("libhoudini.so", RTLD_NOW);
    if (!nb) {
        auto native_bridge = GetNativeBridgeLibrary();
        LOGI("native bridge: %s", native_bridge.data());
        nb = dlopen(native_bridge.data(), RTLD_NOW);
    }
    if (!nb) {
        LOGE("NativeBridge dlopen failed: %s", dlerror());
    }
    if (nb) {
        LOGI("nb %p", nb);
        auto callbacks = (NativeBridgeCallbacks *) dlsym(nb, "NativeBridgeItf");
        if (callbacks) {
            LOGI("NativeBridgeLoadLibrary %p", callbacks->loadLibrary);
            LOGI("NativeBridgeLoadLibraryExt %p", callbacks->loadLibraryExt);
            LOGI("NativeBridgeGetTrampoline %p", callbacks->getTrampoline);

            int fd = (int) syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
            if (fd == -1) {
                LOGE("memfd_create failed");
                munmap(data, length);
                return false;
            }
            if (ftruncate(fd, (off_t) length) == -1) {
                LOGE("ftruncate failed");
                close(fd);
                munmap(data, length);
                return false;
            }
            void *mem = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
            if (mem == MAP_FAILED) {
                LOGE("mmap memfd failed");
                close(fd);
                munmap(data, length);
                return false;
            }
            memcpy(mem, data, length);
            munmap(mem, length);
            munmap(data, length);
            char path[PATH_MAX];
            snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
            LOGI("arm path %s", path);

            void *arm_handle;
            if (api_level >= 26) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }
            if (arm_handle) {
                LOGI("arm handle %p", arm_handle);
                auto init = (void (*)(JavaVM *, void *)) callbacks->getTrampoline(arm_handle,
                                                                                  "JNI_OnLoad",
                                                                                  nullptr, 0);
                LOGI("JNI_OnLoad %p", init);
                if (!init) {
                    LOGE("NativeBridge JNI_OnLoad trampoline missing");
                    close(fd);
                    return false;
                }
                init(vms, (void *) game_data_dir);
                return true;
            }
            close(fd);
        }
    }
    return false;
}

void hack_prepare(
        const char *game_data_dir,
        void *data,
        size_t length,
        JavaVM *vm) {
    LOGI("hack thread: %d", gettid());
    int api_level = android_get_device_api_level();
    LOGI("api level: %d", api_level);

#if defined(__i386__) || defined(__x86_64__)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length, vm)) {
#endif
        hack_start(game_data_dir);
#if defined(__i386__) || defined(__x86_64__)
    }
#endif
}

#if defined(__arm__) || defined(__aarch64__)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}

#endif