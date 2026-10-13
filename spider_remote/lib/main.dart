import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'control_screen.dart';
import 'robot_controller.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Portrait only (the AndroidManifest also enforces this).
  await SystemChrome.setPreferredOrientations([DeviceOrientation.portraitUp]);

  // Create the one RobotController, load saved settings, start polling.
  final robot = RobotController();
  await robot.loadSettings();
  robot.startPolling();

  runApp(SpiderApp(robot: robot));
}

class SpiderApp extends StatelessWidget {
  const SpiderApp({super.key, required this.robot});

  final RobotController robot;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Spider Remote',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark, // dark theme
        colorSchemeSeed: const Color(0xFF2E5FA8),
        useMaterial3: true,
      ),
      home: ControlScreen(robot: robot),
    );
  }
}
