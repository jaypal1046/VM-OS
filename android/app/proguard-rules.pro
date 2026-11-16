# ProGuard / R8 rules for SplitOS

# Preserve JNI methods and VmJniBridge callback
-keep class com.splitos.jni.** { *; }
-keepclassmembers class * {
    native <methods>;
}

# Preserve ForegroundService and TelephonyBridge
-keep class com.splitos.service.** { *; }
-keep class com.splitos.bridge.** { *; }
