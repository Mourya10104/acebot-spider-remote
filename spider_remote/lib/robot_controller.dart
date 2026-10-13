import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:http/io_client.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Default settings (match the firmware in QD020_Spider_Ultrasonic.ino).
const String kDefaultIp = '192.168.4.1';
const String kDefaultApiKey = 'CHANGE_ME';

/// Every request gives up after this long, so the app never freezes.
const Duration kTimeout = Duration(seconds: 2);

/// How often we ask the robot for its status.
const Duration kPollInterval = Duration(milliseconds: 500);

/// Where we are with the robot right now.
enum Link {
  connecting, // haven't heard back yet
  connected, // /status answered 200
  wrongKey, // robot answered 401
  offline, // no answer (wrong WiFi, robot off, ...)
}

/// Holds ALL the app's state and talks to the robot.
///
/// Widgets listen to it (it is a [ChangeNotifier]) and rebuild whenever
/// [notifyListeners] is called.
class RobotController extends ChangeNotifier {
  // ---- Settings (saved on the phone) ----
  String ip = kDefaultIp;
  String apiKey = kDefaultApiKey;

  // ---- Live state shown in the UI ----
  Link link = Link.connecting;
  int distanceCm = -1; // -1 = nothing in range
  bool blocked = false; // obstacle ahead
  String message = ''; // last response text (e.g. the 409 obstacle message)

  late final http.Client _client;
  Timer? _pollTimer;
  bool _polling = false;
  bool _disposed = false;

  // Command queue: only the NEWEST waiting command is kept (see sendCommand).
  String? _pendingCommand;
  bool _sending = false;

  RobotController() {
    // dart:io's HttpClient lets us set a connection timeout.
    final inner = HttpClient()..connectionTimeout = kTimeout;
    _client = IOClient(inner);
  }

  Uri _uri(String path, [Map<String, String>? query]) =>
      Uri(scheme: 'http', host: ip, path: path, queryParameters: query);

  Map<String, String> get _headers => {'X-API-Key': apiKey};

  // -------------------------------------------------------------------------
  // Settings
  // -------------------------------------------------------------------------

  Future<void> loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    ip = prefs.getString('ip') ?? kDefaultIp;
    apiKey = prefs.getString('apiKey') ?? kDefaultApiKey;
    notifyListeners();
  }

  Future<void> saveSettings(String newIp, String newKey) async {
    ip = newIp.trim().isEmpty ? kDefaultIp : newIp.trim();
    apiKey = newKey.trim().isEmpty ? kDefaultApiKey : newKey.trim();
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('ip', ip);
    await prefs.setString('apiKey', apiKey);
    link = Link.connecting;
    notifyListeners();
  }

  // -------------------------------------------------------------------------
  // Status polling (GET /status every 500 ms)
  // -------------------------------------------------------------------------

  void startPolling() {
    _pollTimer?.cancel();
    _pollTimer = Timer.periodic(kPollInterval, (_) => _pollOnce());
    _pollOnce();
  }

  Future<void> _pollOnce() async {
    // If the previous poll is still waiting (slow/offline robot), skip this
    // tick instead of piling up requests.
    if (_polling || _disposed) return;
    _polling = true;
    try {
      final r = await _client.get(_uri('/status'), headers: _headers).timeout(kTimeout);
      if (r.statusCode == 200) {
        final data = jsonDecode(r.body) as Map<String, dynamic>;
        distanceCm = (data['distance'] as num?)?.toInt() ?? -1;
        blocked = data['blocked'] == true;
        link = Link.connected;
      } else if (r.statusCode == 401) {
        link = Link.wrongKey;
      } else {
        link = Link.offline;
      }
    } catch (_) {
      // Timeout, no route, connection refused, bad JSON... all mean "can't talk".
      link = Link.offline;
      distanceCm = -1;
      blocked = false;
    } finally {
      _polling = false;
    }
    if (!_disposed) notifyListeners();
  }

  // -------------------------------------------------------------------------
  // Commands (POST /cmd?a=<command>)
  // -------------------------------------------------------------------------

  /// Queue a command for the robot.
  ///
  /// We send one request at a time. If you press and release a button quickly,
  /// "forward" and "standby" would otherwise race each other over WiFi and
  /// could arrive in the wrong order. So: if a request is already in flight,
  /// we remember only the NEWEST command and send it right after. The robot
  /// always ends up with the last thing you asked for ("standby" on release).
  void sendCommand(String command) {
    _pendingCommand = command;
    _pump();
  }

  Future<void> _pump() async {
    if (_sending) return;
    _sending = true;
    try {
      while (_pendingCommand != null && !_disposed) {
        final command = _pendingCommand!;
        _pendingCommand = null;
        await _post(command);
      }
    } finally {
      _sending = false;
    }
  }

  Future<void> _post(String command) async {
    try {
      final r = await _client
          .post(_uri('/cmd', {'a': command}), headers: _headers)
          .timeout(kTimeout);
      switch (r.statusCode) {
        case 200:
          message = ''; // all good
        case 409:
          message = r.body; // "Obstacle ahead - forward blocked..."
        case 401:
          message = 'Wrong API key - check Settings';
        case 400:
          message = 'Robot did not understand "$command"';
        default:
          message = 'Robot replied ${r.statusCode}';
      }
    } catch (_) {
      message = 'Command "$command" failed - robot not reachable';
    }
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _pollTimer?.cancel();
    _client.close();
    super.dispose();
  }
}
