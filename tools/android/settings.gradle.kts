// ak-mcu-base Android tool: flash and console over USB serial.
//   :proto  fw_proto client in plain Kotlin (JVM), tested against ak_sim
//   :app    the Android app; included only when an Android SDK is installed,
//           so :proto builds and tests on any machine with a JDK
pluginManagement {
	repositories {
		google()
		mavenCentral()
		gradlePluginPortal()
	}
}

dependencyResolutionManagement {
	repositories {
		google()
		mavenCentral()
		maven("https://jitpack.io")	// usb-serial-for-android
	}
}

rootProject.name = "ak-fw-android"

include(":proto")

val localSdk = file("local.properties").takeIf { it.exists() }?.readLines()
	?.firstOrNull { it.startsWith("sdk.dir=") }?.substringAfter("=")
val sdk = localSdk ?: System.getenv("ANDROID_HOME") ?: System.getenv("ANDROID_SDK_ROOT")
if (sdk != null && file(sdk).isDirectory) {
	include(":app")
} else {
	println("ak-fw-android: no Android SDK (ANDROID_HOME / local.properties), building :proto only")
}
