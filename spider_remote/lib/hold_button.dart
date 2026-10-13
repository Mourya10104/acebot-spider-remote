import 'package:flutter/material.dart';

/// A big button that works while it is HELD DOWN.
///
/// [onPress] fires the moment a finger touches it, [onRelease] fires when the
/// finger lifts (or the touch is cancelled, e.g. the system takes over).
/// That's how we send "forward" on press and "standby" on release.
///
/// We use a [Listener] (raw pointer events) instead of a normal button because
/// a normal button only reports a completed "tap", not press and release.
class HoldButton extends StatefulWidget {
  const HoldButton({
    super.key,
    required this.label,
    required this.icon,
    required this.onPress,
    required this.onRelease,
    this.enabled = true,
    this.size = 96,
    this.color = const Color(0xFF2E5FA8),
  });

  final String label;
  final IconData icon;
  final VoidCallback onPress;
  final VoidCallback onRelease;
  final bool enabled;
  final double size; // button height; width comes from the parent layout
  final Color color;

  @override
  State<HoldButton> createState() => _HoldButtonState();
}

class _HoldButtonState extends State<HoldButton> {
  bool _down = false;

  void _press() {
    if (!widget.enabled || _down) return;
    setState(() => _down = true);
    widget.onPress();
  }

  void _release() {
    if (!_down) return;
    setState(() => _down = false);
    widget.onRelease();
  }

  @override
  void didUpdateWidget(HoldButton old) {
    super.didUpdateWidget(old);
    // If the button got disabled while being held (e.g. an obstacle appeared),
    // treat it as released so the robot gets its "standby".
    if (!widget.enabled && _down) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _release();
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final bg = !widget.enabled
        ? const Color(0xFF3A3A3A)
        : (_down ? Color.lerp(widget.color, Colors.white, 0.25)! : widget.color);
    final fg = widget.enabled ? Colors.white : Colors.white38;

    return Listener(
      onPointerDown: (_) => _press(),
      onPointerUp: (_) => _release(),
      onPointerCancel: (_) => _release(),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 60),
        height: widget.size,
        decoration: BoxDecoration(
          color: bg,
          borderRadius: BorderRadius.circular(18),
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(widget.icon, size: widget.size * 0.4, color: fg),
            const SizedBox(height: 2),
            Text(widget.label,
                style: TextStyle(color: fg, fontSize: 13, fontWeight: FontWeight.w600)),
          ],
        ),
      ),
    );
  }
}
