import 'package:flutter/material.dart';

import 'robot_controller.dart';

/// Lets you change the robot's IP address and API key.
/// Values are saved on the phone (SharedPreferences) when you press Save.
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key, required this.robot});

  final RobotController robot;

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  // Controllers hold the text typed into each field.
  late final TextEditingController _ip = TextEditingController(text: widget.robot.ip);
  late final TextEditingController _key =
      TextEditingController(text: widget.robot.apiKey);

  @override
  void dispose() {
    _ip.dispose();
    _key.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    await widget.robot.saveSettings(_ip.text, _key.text);
    if (mounted) Navigator.of(context).pop();
  }

  void _reset() {
    setState(() {
      _ip.text = kDefaultIp;
      _key.text = kDefaultApiKey;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            TextField(
              controller: _ip,
              keyboardType: TextInputType.number,
              decoration: const InputDecoration(
                labelText: 'Robot IP address',
                helperText: 'On the robot WiFi (QuadBot-E) this is $kDefaultIp',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 20),
            TextField(
              controller: _key,
              decoration: const InputDecoration(
                labelText: 'API key',
                helperText: 'Must match API_KEY in the robot firmware',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 28),
            SizedBox(
              width: double.infinity,
              height: 56,
              child: FilledButton(onPressed: _save, child: const Text('Save')),
            ),
            const SizedBox(height: 12),
            TextButton(onPressed: _reset, child: const Text('Reset to defaults')),
          ],
        ),
      ),
    );
  }
}
