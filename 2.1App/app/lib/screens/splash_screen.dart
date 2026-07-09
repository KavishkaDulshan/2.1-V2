import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/ble_state.dart';
import '../services/preferences_service.dart';
import 'dashboard_screen.dart';
import 'scan_screen.dart';

class SplashScreen extends StatefulWidget {
  const SplashScreen({super.key});

  @override
  State<SplashScreen> createState() => _SplashScreenState();
}

class _SplashScreenState extends State<SplashScreen> {
  String _statusMessage = 'Initializing...';

  @override
  void initState() {
    super.initState();
    _checkPreferencesAndConnect();
  }

  Future<void> _checkPreferencesAndConnect() async {
    final pairing = await PreferencesService.getRobotPairing();
    final macAddress = pairing['mac'];
    final ssid = pairing['ssid'];
    final pass = pairing['pass'];

    if (macAddress != null && ssid != null && pass != null) {
      setState(() => _statusMessage = 'Found saved robot. Connecting...');
      
      if (!mounted) return;
      final ble = context.read<BleState>();
      
      bool connected = await ble.connectByMacAddress(macAddress);
      
      if (connected) {
        setState(() => _statusMessage = 'Provisioning Wi-Fi...');
        
        // Wait a brief moment for characteristics to settle
        await Future.delayed(const Duration(seconds: 1));
        
        bool success = await ble.sendWifiCredentials(ssid, pass);
        
        if (success) {
          ble.disconnect(); // Clean up BLE once provisioned
          if (!mounted) return;
          Navigator.of(context).pushReplacement(
            MaterialPageRoute(builder: (_) => const DashboardScreen()),
          );
          return;
        } else {
          setState(() => _statusMessage = 'Failed to provision. Retrying later.');
        }
      } else {
        setState(() => _statusMessage = 'Robot not found nearby.');
      }
      
      // If it failed to connect or provision, fall back to scan screen after a delay
      await Future.delayed(const Duration(seconds: 2));
      if (!mounted) return;
      Navigator.of(context).pushReplacement(
        MaterialPageRoute(builder: (_) => const ScanScreen()),
      );

    } else {
      // No robot saved, go straight to scan screen
      if (!mounted) return;
      Navigator.of(context).pushReplacement(
        MaterialPageRoute(builder: (_) => const ScanScreen()),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.smart_toy, size: 100, color: Colors.blueAccent),
            const SizedBox(height: 30),
            const SizedBox(
              width: 40, height: 40,
              child: CircularProgressIndicator(color: Colors.blueAccent),
            ),
            const SizedBox(height: 30),
            Text(
              _statusMessage,
              style: const TextStyle(
                color: Colors.white70,
                fontSize: 16,
                fontWeight: FontWeight.w500,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
