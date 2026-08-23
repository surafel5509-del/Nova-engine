# Phase 1: no code shrinking/obfuscation yet.
# Native JNI methods are referenced by name from C++; keep the bridge class.
-keep class dev.nova.editor.bridge.** { *; }
