plugins {
	id("com.android.application") version "8.7.3"
	kotlin("android") version "2.0.21"
}

android {
	namespace = "ak.mcu.fw"
	compileSdk = 35

	defaultConfig {
		applicationId = "ak.mcu.fw"
		minSdk = 21
		targetSdk = 35
		versionCode = 1
		versionName = "0.1.0"
	}

	buildTypes {
		release {
			isMinifyEnabled = false
		}
	}

	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_11
		targetCompatibility = JavaVersion.VERSION_11
	}
}

kotlin {
	compilerOptions {
		jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_11)
	}
}

dependencies {
	implementation(project(":proto"))
	implementation("com.github.mik3y:usb-serial-for-android:3.8.1")
	implementation("androidx.activity:activity-ktx:1.9.3")
}
