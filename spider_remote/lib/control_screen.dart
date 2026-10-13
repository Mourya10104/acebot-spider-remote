import 'package:flutter/material.dart';

import 'hold_button.dart';
import 'robot_controller.dart';
import 'settings_screen.dart';

/// The main screen: status at the top, then D-pad, STOP and pose buttons.
class ControlScreen extends StatelessWidget {
  const ControlScreen({super.key, required this.robot});

  final RobotController robot;

  // Pose buttons: (command sent to the robot, label, icon).
  static const poses = <(String, String, IconData)>[
    ('hello', 'Hello', Icons.waving_hand),
    ('sleep', 'Sleep', Icons.bedtime),
    ('lie', 'Lie', Icons.airline_seat_flat),
    ('pushup', 'Push-up', Icons.fitness_center),
    ('fight', 'Fight', Icons.sports_mma),
    ('handsome', 'Handsome', Icons.face),
    ('dance1', 'Dance 1', Icons.music_note),
    ('dance2', 'Dance 2', Icons.queue_music),
    ('dance3', 'Dance 3', Icons.celebration),
  ];

  @override
  Widget build(BuildContext context) {
    // ListenableBuilder rebuilds this part whenever the robot state changes.
    return ListenableBuilder(
      listenable: robot,
      builder: (context, _) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('Spider Remote'),
            actions: [
              IconButton(
                icon: const Icon(Icons.settings),
                tooltip: 'Settings',
                onPressed: () => Navigator.of(context).push(
                  MaterialPageRoute(builder: (_) => SettingsScreen(robot: robot)),
                ),
              ),
            ],
          ),
          body: SafeArea(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  _StatusCard(robot: robot),
                  const SizedBox(height: 16),
                  _dpad(),
                  const SizedBox(height: 12),
                  _strafeRow(),
                  const SizedBox(height: 16),
                  _stopButton(),
                  const SizedBox(height: 20),
                  const Align(
                    alignment: Alignment.centerLeft,
                    child: Text('Poses',
                        style: TextStyle(fontSize: 16, color: Colors.white70)),
                  ),
                  const SizedBox(height: 8),
                  _poseGrid(),
                ],
              ),
            ),
          ),
        );
      },
    );
  }

  /// A movement button: sends [command] on press and "standby" on release.
  Widget _move(String label, IconData icon, String command,
      {bool enabled = true, double size = 96}) {
    return HoldButton(
      label: label,
      icon: icon,
      size: size,
      enabled: enabled,
      onPress: () => robot.sendCommand(command),
      onRelease: () => robot.sendCommand('standby'),
    );
  }

  /// Cross-shaped D-pad:      [ Forward ]
  ///                    [Left]          [Right]
  ///                          [ Back  ]
  Widget _dpad() {
    const gap = SizedBox(width: 10);
    return Column(
      children: [
        Row(children: [
          const Spacer(),
          Expanded(
            // Grey out Forward while the robot reports an obstacle.
            child: _move('Forward', Icons.keyboard_arrow_up, 'forward',
                enabled: !robot.blocked),
          ),
          const Spacer(),
        ]),
        const SizedBox(height: 10),
        Row(children: [
          Expanded(child: _move('Turn left', Icons.rotate_left, 'turn_left')),
          gap,
          const Spacer(),
          gap,
          Expanded(child: _move('Turn right', Icons.rotate_right, 'turn_right')),
        ]),
        const SizedBox(height: 10),
        Row(children: [
          const Spacer(),
          Expanded(child: _move('Backward', Icons.keyboard_arrow_down, 'backward')),
          const Spacer(),
        ]),
      ],
    );
  }

  Widget _strafeRow() {
    return Row(children: [
      Expanded(
          child: _move('Strafe left', Icons.arrow_back, 'strafe_left', size: 64)),
      const SizedBox(width: 10),
      Expanded(
          child: _move('Strafe right', Icons.arrow_forward, 'strafe_right', size: 64)),
    ]);
  }

  Widget _stopButton() {
    return SizedBox(
      width: double.infinity,
      height: 80,
      child: FilledButton.icon(
        style: FilledButton.styleFrom(
          backgroundColor: const Color(0xFFD32F2F),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(18)),
        ),
        // Stop = the robot's "standby" command.
        onPressed: () => robot.sendCommand('standby'),
        icon: const Icon(Icons.stop_circle, size: 36),
        label: const Text('STOP',
            style: TextStyle(fontSize: 28, fontWeight: FontWeight.bold)),
      ),
    );
  }

  Widget _poseGrid() {
    return GridView.count(
      crossAxisCount: 3,
      shrinkWrap: true, // let the grid size itself inside the scroll view
      physics: const NeverScrollableScrollPhysics(),
      mainAxisSpacing: 10,
      crossAxisSpacing: 10,
      childAspectRatio: 1.15,
      children: [
        for (final (cmd, label, icon) in poses)
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: const Color(0xFF6A3FB5),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(18)),
              padding: EdgeInsets.zero,
            ),
            onPressed: () => robot.sendCommand(cmd),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(icon, size: 30),
                const SizedBox(height: 4),
                Text(label, style: const TextStyle(fontSize: 13)),
              ],
            ),
          ),
      ],
    );
  }
}

/// Connection indicator, live distance, and the red obstacle banner.
class _StatusCard extends StatelessWidget {
  const _StatusCard({required this.robot});

  final RobotController robot;

  @override
  Widget build(BuildContext context) {
    final connected = robot.link == Link.connected;
    final (dotColor, statusText) = switch (robot.link) {
      Link.connected => (Colors.greenAccent, 'Connected'),
      Link.connecting => (Colors.amber, 'Connecting...'),
      Link.wrongKey => (Colors.redAccent, 'Wrong API key'),
      Link.offline => (Colors.redAccent, 'Not connected'),
    };

    final distanceText = !connected
        ? '--'
        : robot.distanceCm < 0
            ? 'Clear'
            : '${robot.distanceCm} cm';

    return Card(
      color: const Color(0xFF1E1E1E),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Row(children: [
              Icon(Icons.circle, size: 14, color: dotColor),
              const SizedBox(width: 8),
              Text(statusText, style: const TextStyle(fontSize: 16)),
              const Spacer(),
              Text(robot.ip, style: const TextStyle(color: Colors.white54)),
            ]),
            if (!connected && robot.link != Link.connecting) ...[
              const SizedBox(height: 8),
              Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  robot.link == Link.wrongKey
                      ? 'The robot rejected the API key. Check Settings.'
                      : 'Connect your phone to the QuadBot-E WiFi',
                  style: const TextStyle(color: Colors.amber),
                ),
              ),
            ],
            const SizedBox(height: 12),
            const Text('Distance ahead',
                style: TextStyle(color: Colors.white54, fontSize: 13)),
            Text(distanceText,
                style: TextStyle(
                  fontSize: 44,
                  fontWeight: FontWeight.bold,
                  color: robot.blocked ? Colors.redAccent : Colors.white,
                )),
            if (robot.blocked) ...[
              const SizedBox(height: 8),
              Container(
                width: double.infinity,
                padding: const EdgeInsets.symmetric(vertical: 10),
                decoration: BoxDecoration(
                  color: const Color(0xFFD32F2F),
                  borderRadius: BorderRadius.circular(10),
                ),
                child: const Text('Obstacle ahead!',
                    textAlign: TextAlign.center,
                    style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
              ),
            ],
            // Last message from the robot (e.g. the 409 text or an error).
            if (robot.message.isNotEmpty) ...[
              const SizedBox(height: 8),
              Text(robot.message,
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: Colors.orangeAccent)),
            ],
          ],
        ),
      ),
    );
  }
}
