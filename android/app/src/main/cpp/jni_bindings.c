#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "vm_core/vm_engine.h"
#include "display/display_bridge.h"
#include "serial/uart_ipc.h"

#ifdef __ANDROID__
#include <android/native_window_jni.h>
#include <android/log.h>
#define LOG_TAG "VmJniBindings"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf("[VmJniBindings INFO] " __VA_ARGS__); printf("\n")
#define LOGE(...) fprintf(stderr, "[VmJniBindings ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
#endif

static JavaVM* g_jvm = NULL;
static jclass g_bridge_class = NULL;
static jmethodID g_on_guest_ipc_mid = NULL;

static void on_uart_guest_command(const char* cmd) {
    if (!g_jvm || !g_bridge_class || !g_on_guest_ipc_mid || !cmd) return;

    JNIEnv* env = NULL;
    int need_detach = 0;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**)&env, JNI_VERSION_1_6);

    if (res == JNI_EDETACHED) {
#ifdef __ANDROID__
        if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != JNI_OK) {
            LOGE("Failed to attach current thread to JVM");
            return;
        }
#else
        if ((*g_jvm)->AttachCurrentThread(g_jvm, (void**)&env, NULL) != JNI_OK) {
            return;
        }
#endif
        need_detach = 1;
    } else if (res != JNI_OK) {
        LOGE("Failed to get JNIEnv from JavaVM");
        return;
    }

    jstring j_cmd = (*env)->NewStringUTF(env, cmd);
    if (j_cmd) {
        (*env)->CallStaticVoidMethod(env, g_bridge_class, g_on_guest_ipc_mid, j_cmd);
        (*env)->DeleteLocalRef(env, j_cmd);
    }

    if (need_detach) {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    g_jvm = vm;

    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("JNI_OnLoad: GetEnv failed");
        return JNI_ERR;
    }

    jclass local_class = (*env)->FindClass(env, "com/splitos/jni/VmJniBridge");
    if (!local_class) {
        LOGE("JNI_OnLoad: Could not find VmJniBridge class");
        return JNI_ERR;
    }

    g_bridge_class = (jclass)(*env)->NewGlobalRef(env, local_class);
    (*env)->DeleteLocalRef(env, local_class);

    g_on_guest_ipc_mid = (*env)->GetStaticMethodID(env, g_bridge_class, "onGuestIpcMessage", "(Ljava/lang/String;)V");
    if (!g_on_guest_ipc_mid) {
        LOGE("JNI_OnLoad: Could not find onGuestIpcMessage method ID");
        return JNI_ERR;
    }

    uart_ipc_init(on_uart_guest_command);
    LOGI("JNI_OnLoad: Initialized successfully.");
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    (void)reserved;
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        if (g_bridge_class) {
            (*env)->DeleteGlobalRef(env, g_bridge_class);
            g_bridge_class = NULL;
        }
    }
    uart_ipc_cleanup();
    LOGI("JNI_OnUnload: Cleaned up.");
}

JNIEXPORT jboolean JNICALL
Java_com_splitos_jni_VmJniBridge_nativeInitVm(JNIEnv* env, jobject thiz, jstring asset_path) {
    (void)thiz;
    const char* path_str = asset_path ? (*env)->GetStringUTFChars(env, asset_path, NULL) : NULL;
    int res = vm_engine_init(path_str);
    if (path_str) {
        (*env)->ReleaseStringUTFChars(env, asset_path, path_str);
    }
    return res == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_splitos_jni_VmJniBridge_nativeStartVm(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    return vm_engine_start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_splitos_jni_VmJniBridge_nativeStopVm(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    return vm_engine_stop() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_splitos_jni_VmJniBridge_nativePauseVm(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    return vm_engine_pause() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_splitos_jni_VmJniBridge_nativeResumeVm(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    return vm_engine_resume() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_splitos_jni_VmJniBridge_nativeSetSurface(JNIEnv* env, jobject thiz, jobject surface) {
    (void)thiz;
#ifdef __ANDROID__
    if (surface != NULL) {
        ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
        if (window) {
            display_bridge_set_window(window);
            ANativeWindow_release(window); // display_bridge retains its own reference via acquire
            return JNI_TRUE;
        }
        return JNI_FALSE;
    } else {
        display_bridge_set_window(NULL);
        return JNI_TRUE;
    }
#else
    (void)env;
    (void)surface;
    return JNI_TRUE;
#endif
}

JNIEXPORT void JNICALL
Java_com_splitos_jni_VmJniBridge_nativeSendTouchInput(JNIEnv* env, jobject thiz, jfloat x, jfloat y, jint action) {
    (void)env;
    (void)thiz;
    vm_engine_send_touch((float)x, (float)y, (int)action);
}

JNIEXPORT void JNICALL
Java_com_splitos_jni_VmJniBridge_nativeSetDisplayMode(JNIEnv* env, jobject thiz, jint mode) {
    (void)env;
    (void)thiz;
    vm_engine_set_display_mode((int)mode);
}

JNIEXPORT void JNICALL
Java_com_splitos_jni_VmJniBridge_nativeSendSerialMessage(JNIEnv* env, jobject thiz, jstring command) {
    (void)thiz;
    if (!command) return;
    const char* str = (*env)->GetStringUTFChars(env, command, NULL);
    if (str) {
        uart_ipc_host_write(str, strlen(str));
        (*env)->ReleaseStringUTFChars(env, command, str);
    }
}

JNIEXPORT jint JNICALL
Java_com_splitos_jni_VmJniBridge_nativeGetVmState(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    return (jint)vm_engine_get_state();
}

JNIEXPORT jlong JNICALL
Java_com_splitos_jni_VmJniBridge_nativeGetRamUsage(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    return (jlong)vm_engine_get_ram_usage();
}
