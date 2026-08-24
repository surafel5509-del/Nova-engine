plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.serialization")
}

/**
 * Standalone game app: packages a Nova project (passed via
 * -PnovaProjectPath=...) into a self-contained APK. Reuses the engine
 * (CMake), the JNI bridge, the runtime surface, the scene model, and the
 * audio engine from :app via shared source dirs.
 */
android {
    namespace = "dev.nova.game"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "dev.nova.game"
        minSdk = 21
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-fexceptions", "-frtti")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../engine/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlin {
        compilerOptions {
            jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        }
    }

    sourceSets["main"].java.srcDirs(
        "../app/src/main/java/dev/nova/editor/bridge",
        "../app/src/main/java/dev/nova/editor/gameruntime",
        "../app/src/main/java/dev/nova/editor/audio",
        "../app/src/main/java/dev/nova/editor/scene",
    )
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("novaAssets"))
}

val novaProjectPath = project.findProperty("novaProjectPath") as String?

val packageProject = tasks.register<Copy>("packageNovaProject") {
    // Package the project into assets/project/.
    val pathProvider = novaProjectPath ?: ""
    if (pathProvider.isNotBlank()) {
        from(pathProvider) {
            include("project.json")
            include("scenes/**")
            include("assets/**")
            include("scripts/**")
        }
    }
    into(layout.buildDirectory.dir("novaAssets/project"))
}

tasks.named("preBuild").configure { dependsOn(packageProject) }

dependencies {
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.3")
}
