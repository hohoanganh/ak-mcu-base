plugins {
	kotlin("jvm") version "2.0.21"
}

// Java 11 bytecode: runs on any JDK 11+ and is accepted by the Android build
java {
	sourceCompatibility = JavaVersion.VERSION_11
	targetCompatibility = JavaVersion.VERSION_11
}

kotlin {
	compilerOptions {
		jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_11)
	}
}

dependencies {
	testImplementation(kotlin("test"))
	testImplementation("org.junit.jupiter:junit-jupiter:5.10.3")
	testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

tasks.test {
	useJUnitPlatform()
	// SimOtaTest: AK_SIM=<ak_sim> runs the client against the simulator
	systemProperty("ak.tools", rootDir.resolve("..").absolutePath)
	testLogging {
		events("passed", "skipped", "failed")
		exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL
	}
}
