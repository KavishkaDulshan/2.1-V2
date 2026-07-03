import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/ble_state.dart';

class ProvisionScreen extends StatefulWidget {
  const ProvisionScreen({super.key});

  @override
  State<ProvisionScreen> createState() => _ProvisionScreenState();
}

class _ProvisionScreenState extends State<ProvisionScreen> {
  final _ssidController = TextEditingController();
  final _passController = TextEditingController();
  bool _isSending = false;

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleState>();
    final device = ble.connectedDevice;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Connect to Wi-Fi'),
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () {
            ble.disconnect();
            Navigator.pop(context);
          },
        ),
      ),
      body: Padding(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const Icon(Icons.wifi, size: 80, color: Colors.blueAccent),
            const SizedBox(height: 24),
            Text(
              'Connected to ${device?.advName ?? "Robot"}',
              style: Theme.of(context).textTheme.titleLarge,
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 8),
            const Text(
              'Enter your home Wi-Fi credentials below to get the robot online.',
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 32),
            TextField(
              controller: _ssidController,
              decoration: const InputDecoration(
                labelText: 'Network Name (SSID)',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.router),
              ),
            ),
            const SizedBox(height: 16),
            TextField(
              controller: _passController,
              obscureText: true,
              decoration: const InputDecoration(
                labelText: 'Password',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.lock),
              ),
            ),
            const SizedBox(height: 32),
            FilledButton(
              style: FilledButton.styleFrom(
                padding: const EdgeInsets.all(16),
              ),
              onPressed: _isSending ? null : () async {
                setState(() => _isSending = true);
                
                bool success = await ble.sendWifiCredentials(
                  _ssidController.text,
                  _passController.text,
                );
                
                setState(() => _isSending = false);
                
                if (success && context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('Credentials Sent! The robot will now connect to Wi-Fi.')),
                  );
                  ble.disconnect();
                  Navigator.pop(context);
                } else {
                  if (context.mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(content: Text('Failed to send credentials.')),
                    );
                  }
                }
              },
              child: _isSending
                  ? const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(color: Colors.white))
                  : const Text('Send to Robot', style: TextStyle(fontSize: 16)),
            ),
          ],
        ),
      ),
    );
  }
}
