import 'dart:isolate';
import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'models/ble_state.dart';
import 'models/mqtt_state.dart';
import 'screens/splash_screen.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:flutter_notification_listener/flutter_notification_listener.dart';
import 'package:shared_preferences/shared_preferences.dart';
// ─── Allowed apps filter ──────────────────────────────────────────────────────
const Map<String, String> _allowedApps = {
  'com.whatsapp':                        'whatsapp',
  'com.whatsapp.w4b':                    'whatsapp',
  'com.google.android.apps.messaging':  'sms',
  'com.android.mms':                     'sms',
  'com.samsung.android.messaging':       'sms',
  'com.android.dialer':                  'phone',
  'com.samsung.android.dialer':          'phone',
  'com.google.android.dialer':           'phone',
  'org.telegram.messenger':              'telegram',
  'com.google.android.gm':              'gmail',
  'com.google.android.youtube':         'youtube',
  'com.facebook.katana':                 'facebook',
  'com.facebook.orca':                   'facebook',
  'com.instagram.android':              'instagram',
  'com.zhiliaoapp.musically':            'tiktok',
  'com.tiktok.musically':                'tiktok',
};

// ============================================================================
// CRITICAL: We MUST define our own callback because the plugin's default one 
// is missing this @pragma annotation, which causes a DartVM crash!
// ============================================================================
@pragma('vm:entry-point')
void customNotificationCallback(NotificationEvent evt) {
  // Send the event directly to the plugin's built-in receive port
  final SendPort? sendPort = IsolateNameServer.lookupPortByName(NotificationsListener.SEND_PORT_NAME);
  if (sendPort != null) {
    sendPort.send(evt);
  } else {
    debugPrint('[BG] Could not find port: ${NotificationsListener.SEND_PORT_NAME}');
  }
}

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  debugPrint('[MAIN] Initializing plugin...');
  // Initialize with our custom callback to avoid the missing pragma crash
  await NotificationsListener.initialize(callbackHandle: customNotificationCallback);

  final hasPermission = await NotificationsListener.hasPermission ?? false;
  debugPrint('[MAIN] Notification permission: $hasPermission');

  if (hasPermission) {
    final isRunning = await NotificationsListener.isRunning ?? false;
    debugPrint('[MAIN] Service running: $isRunning');
    
    if (!isRunning) {
      debugPrint('[MAIN] Starting service as foreground...');
      await NotificationsListener.startService(
        foreground: true,
        title: 'Robot Notification Watcher',
        description: 'Monitoring for WhatsApp, Telegram & more',
      );
    }
  }

  // Bluetooth permissions
  await [
    Permission.bluetoothScan,
    Permission.bluetoothConnect,
    Permission.location,
  ].request();

  runApp(
    MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => BleState()),
        ChangeNotifierProvider(create: (_) => MqttState()),
      ],
      child: const MyApp(),
    ),
  );
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});
  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  @override
  void initState() {
    super.initState();
    _startListening();
  }

  void _startListening() {
    debugPrint('[MAIN] Attaching to plugin receivePort...');
    NotificationsListener.receivePort?.listen((dynamic evt) {
      debugPrint('[MAIN] ← Event from plugin: $evt  type=${evt.runtimeType}');
      if (evt is NotificationEvent) {
        _handleNotificationEvent(evt);
      }
    });
    debugPrint('[MAIN] receivePort listener attached.');
  }

  void _handleNotificationEvent(NotificationEvent evt) async {
    debugPrint('[NOTIF] pkg=${evt.packageName}  title=${evt.title}');

    if (evt.packageName == null) return;
    if (!_allowedApps.containsKey(evt.packageName)) {
      return;
    }

    final appName = _allowedApps[evt.packageName]!;
    
    // Check SharedPreferences for toggles
    final prefs = await SharedPreferences.getInstance();
    final bool masterToggle = prefs.getBool('notif_master') ?? false;
    if (!masterToggle) {
      debugPrint('[NOTIF] Master toggle is OFF. Dropping notification.');
      return;
    }
    
    final bool appToggle = prefs.getBool('notif_app_$appName') ?? false;
    if (!appToggle) {
      debugPrint('[NOTIF] Toggle for $appName is OFF. Dropping notification.');
      return;
    }

    String sender = (evt.title ?? 'Someone').replaceAll('"', '\\"');
    if (sender.length > 20) sender = sender.substring(0, 20);

    debugPrint('[NOTIF] ✓ Forwarding → app=$appName  sender=$sender');

    if (!mounted) return;

    final mqttState = context.read<MqttState>();
    if (mqttState.isConnected) {
      final payload = '{"notify": "$appName", "sender": "$sender"}';
      mqttState.publish('robot21/commands/master', payload);
      debugPrint('[NOTIF] Published: $payload');
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Robot 2.1 Control',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blueAccent,
          brightness: Brightness.light,
        ),
        useMaterial3: true,
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blueAccent,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      themeMode: ThemeMode.system,
      home: const SplashScreen(),
    );
  }
}
