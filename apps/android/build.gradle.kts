plugins {
    alias(libs.plugins.android.application) apply false
    // Puts this Kotlin version on the build classpath in place of the one
    // the Android plugin bundles; the Compose compiler must match it.
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.compose) apply false
}
