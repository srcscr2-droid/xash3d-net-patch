plugins {
    id("com.android.application")
}
android {
    namespace = "su.xash.engine"
    compileSdk = 33
    defaultConfig {
        applicationId = "su.xash.engine"
        minSdk = 21
        targetSdk = 33
        versionCode = 1
        versionName = "1.0-net"
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    sourceSets {
        getByName("main") {
            jniLibs.srcDirs("src/main/jniLibs")
        }
    }
    packaging {
        jniLibs.useLegacyPackaging = true
    }
}
