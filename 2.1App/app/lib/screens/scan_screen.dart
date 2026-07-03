import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/ble_state.dart';
import 'provision_screen.dart';

class ScanScreen extends StatefulWidget {
  const ScanScreen({super.key});

  @override
  State<ScanScreen> createState() => _ScanScreenState();
}

class _ScanScreenState extends State<ScanScreen> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      context.read<BleState>().startScan();
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Find Robot 2.1'),
        actions: [
          Consumer<BleState>(
            builder: (context, ble, child) {
              return ble.isScanning 
                ? const Padding(
                    padding: EdgeInsets.all(16.0),
                    child: SizedBox(
                      width: 20, height: 20,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    ),
                  )
                : IconButton(
                    icon: const Icon(Icons.refresh),
                    onPressed: () => ble.startScan(),
                  );
            },
          )
        ],
      ),
      body: Consumer<BleState>(
        builder: (context, ble, child) {
          if (ble.scanResults.isEmpty) {
            return Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.bluetooth_searching, size: 80, color: Theme.of(context).colorScheme.primary.withValues(alpha: 0.5)),
                  const SizedBox(height: 16),
                  Text(
                    ble.isScanning ? 'Searching for Robot 2.1...' : 'No robot found nearby.',
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                ],
              ),
            );
          }

          return ListView.builder(
            itemCount: ble.scanResults.length,
            itemBuilder: (context, index) {
              final result = ble.scanResults[index];
              final deviceName = result.device.advName.isNotEmpty ? result.device.advName : "Unknown Device";
              
              return Card(
                margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                elevation: 0,
                color: Theme.of(context).colorScheme.surfaceContainerHighest,
                child: ListTile(
                  leading: const CircleAvatar(
                    child: Icon(Icons.smart_toy),
                  ),
                  title: Text(deviceName, style: const TextStyle(fontWeight: FontWeight.bold)),
                  subtitle: Text(result.device.remoteId.str),
                  trailing: FilledButton(
                    onPressed: () async {
                      bool success = await ble.connectToDevice(result.device);
                      if (success && context.mounted) {
                        Navigator.push(
                          context,
                          MaterialPageRoute(builder: (_) => const ProvisionScreen()),
                        );
                      } else {
                        if (context.mounted) {
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(content: Text('Failed to connect to the robot.')),
                          );
                        }
                      }
                    },
                    child: const Text('Connect'),
                  ),
                ),
              );
            },
          );
        },
      ),
    );
  }
}
