# ACEBOTT QD020 Spider Remote

Control an **ACEBOTT QD020** quadruped spider robot (ESP8266) from an Android phone.

The project has two parts:

| Part | Folder / file | What it is |
|---|---|---|
| Firmware | `QD020_Spider_Ultrasonic.ino` | ESP8266 sketch: walking gaits, poses, web/HTTP API, and an **HC-SR04 ultrasonic sensor** that stops the robot from walking into obstacles |
| Android app | `spider_remote/` | Flutter remote control: hold-to-move D-pad, STOP, pose buttons, live distance, connection indicator, settings |

The robot creates its own WiFi network (`QuadBot-E`, password `12345678`, address `http://192.168.4.1`). The phone joins that network and sends HTTP commands.

## Before you start: set your own WiFi and API key

The published code contains **placeholders only**. You must set your own values:

1. **In the firmware**, open `QD020_Spider_Ultrasonic.ino` and, **before uploading**, change:
   - `STA_SSID` = `"YOUR_WIFI_NAME"`: your home WiFi name (optional but needed if you want the robot on your home network)
   - `STA_PASS` = `"YOUR_WIFI_PASSWORD"`: your home WiFi password
   - `API_KEY` = `"CHANGE_ME"`: a long random secret of your own (for example 32 random hex characters)
2. **In the Android app**, open **Settings** (gear icon) and enter **the same key** you put in `API_KEY`. Until they match, the robot answers `401` and the app shows "Wrong API key".

Never commit your real WiFi password or API key. Keep them in a local copy such as `QD020_Spider_Ultrasonic.local.ino`, which `.gitignore` excludes.

## Wiring: ultrasonic sensor on the P7 header

The sensor uses the ESP8266's TX/RX pins, so **the serial monitor is not available** while it is connected.

| HC-SR04 pin | Spider shield P7 pin | Notes |
|---|---|---|
| VCC | 5V | |
| GND | GND | |
| Trig | TX (GPIO1) | |
| Echo | RX (GPIO3) | **Through a voltage divider** (below) |

The Echo pin outputs 5 V but the ESP8266 is only 3.3 V tolerant:

```
Echo ──[ 1 kΩ ]──┬── RX (GPIO3)
                 │
               [ 2 kΩ ]
                 │
                GND
```

## Uploading the firmware (Arduino IDE)

1. Install the ESP8266 board package (Boards Manager → `esp8266`).
2. Install the **Adafruit MQTT Library** (Library Manager).
3. Select board **NodeMCU 1.0 (ESP-12E Module)**.
4. **Unplug the sensor (at least the Echo wire) before uploading**: the sensor shares the serial pins.
5. Fill in the placeholders near the top of `QD020_Spider_Ultrasonic.ino`:

   | Constant | Replace with |
   |---|---|
   | `STA_SSID` / `STA_PASS` | Your home WiFi name / password (optional; the robot also joins it so you can reach it from your network) |
   | `API_KEY` | A long random secret of your choice |
   | `AIO_*` | Only if you want remote control over Adafruit IO MQTT; otherwise leave them (connect attempts just fail harmlessly) |

   Tip: keep your real values in a copy named `QD020_Spider_Ultrasonic.local.ino`; it is git-ignored.
6. Upload, then plug the sensor back in and run the robot **on battery with the USB cable unplugged** (the USB chip disturbs the RX line and masks the Echo signal).

## Building and installing the app

Requirements: [Flutter](https://docs.flutter.dev/get-started/install) (stable) with the Android SDK.

```bash
cd spider_remote
flutter pub get
flutter build apk --debug
```

The APK is created at `spider_remote/build/app/outputs/flutter-apk/app-debug.apk`.

Install it by either:

- **USB:** enable *Developer options → USB debugging* on the phone, plug it in, then `adb install -r app-debug.apk`; or
- **File copy:** copy the APK to the phone, open it, and allow "Install unknown apps" when asked.

Using the app:

1. Join the **QuadBot-E** WiFi (accept "stay connected" if Android warns there is no internet).
2. Open **Settings** (gear icon) and enter the same **API key** you put in the firmware. The robot IP defaults to `192.168.4.1`.
3. Hold a direction button to move; releasing sends `standby`. The red STOP button stops immediately.

The app binds its traffic to WiFi so Android doesn't route requests over mobile data when the robot's network has no internet.

## HTTP API

Plain HTTP on port 80. Send the key in the `X-API-Key` header (or `&key=` query argument).

```
POST http://192.168.4.1/cmd?a=<command>
GET  http://192.168.4.1/status
```

| Command | Action |
|---|---|
| `forward`, `backward` | Walk |
| `turn_left`, `turn_right` | Turn on the spot |
| `strafe_left`, `strafe_right` | Sidestep |
| `standby` (or `stop`) | Stop and stand |
| `hello`, `sleep`, `lie`, `pushup`, `fight`, `handsome`, `dance1`, `dance2`, `dance3` | Poses / routines |

Movement commands run for 10 seconds and then stop on their own.

| Response | Meaning |
|---|---|
| `200` | OK |
| `400` | Unknown command |
| `401` | Missing or wrong API key |
| `409` | Obstacle ahead: `forward` refused |

`GET /status` returns `{"distance":23,"blocked":false,"ip":"..."}`: distance in cm (`-1` = nothing in range), the obstacle flag, and the home-WiFi IP.

## Security note

The API key is a shared secret sent over plain HTTP. It keeps casual users on the same network out, but it is not real authentication. Don't commit your real WiFi password or API key.

## License

[MIT](LICENSE)
