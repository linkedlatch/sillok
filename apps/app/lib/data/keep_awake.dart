// Keeps the computer from sleeping while files are transcribed (DESIGN D34):
// idle sleep only, so the screen still turns off and a laptop still sleeps
// when its lid closes. On macOS the same activity keeps the app out of App
// Nap, which would slow a run whose window is hidden.
import 'dart:ffi';
import 'dart:io';

import 'package:flutter/services.dart';

abstract interface class KeepAwake {
  /// This system's way, or none where there is no such way.
  factory KeepAwake.system() => Platform.isWindows
      ? _WindowsKeepAwake()
      : Platform.isMacOS
      ? _MacKeepAwake()
      : const NoKeepAwake();

  /// Keeps the computer awake until [release].
  void hold();

  void release();
}

final class NoKeepAwake implements KeepAwake {
  const NoKeepAwake();

  @override
  void hold() {}

  @override
  void release() {}
}

/// SetThreadExecutionState, from the platform thread the app's Dart code runs
/// on, which keeps the state until it changes it.
final class _WindowsKeepAwake implements KeepAwake {
  static final _set = DynamicLibrary.open('kernel32.dll')
      .lookupFunction<Uint32 Function(Uint32), int Function(int)>('SetThreadExecutionState');
  static const _continuous = 0x80000000;
  static const _systemRequired = 0x00000001;

  @override
  void hold() => _set(_continuous | _systemRequired);

  @override
  void release() => _set(_continuous);
}

/// ProcessInfo.beginActivity in the app's window (macos/Runner).
final class _MacKeepAwake implements KeepAwake {
  static const _channel = MethodChannel('sillok/keep_awake');

  @override
  void hold() => _channel.invokeMethod<void>('hold');

  @override
  void release() => _channel.invokeMethod<void>('release');
}
