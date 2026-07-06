import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BleState extends ChangeNotifier {
  bool isScanning = false;
  BluetoothDevice? connectedDevice;
  BluetoothCharacteristic? provisionCharacteristic;

  // UUIDs matching the ESP32 NimBLE Server
  final String serviceUuid = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
  final String charUuid = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

  StreamSubscription? _scanSub;
  StreamSubscription? _connSub;

  List<ScanResult> scanResults = [];

  void startScan() async {
    isScanning = true;
    scanResults.clear();
    notifyListeners();

    try {
      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 15),
      );

      _scanSub = FlutterBluePlus.scanResults.listen((results) {
        scanResults = results.where((r) => r.device.advName.toLowerCase().contains('robot')).toList();
        notifyListeners();
      });

      // Wait for scan to finish
      await Future.delayed(const Duration(seconds: 15));
    } catch (e) {
      debugPrint("Scan Error: $e");
    } finally {
      stopScan();
    }
  }

  void stopScan() async {
    await FlutterBluePlus.stopScan();
    _scanSub?.cancel();
    isScanning = false;
    notifyListeners();
  }

  Future<bool> connectToDevice(BluetoothDevice device) async {
    stopScan();
    
    try {
      await device.connect(autoConnect: false);
      connectedDevice = device;
      
      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          connectedDevice = null;
          provisionCharacteristic = null;
          notifyListeners();
        }
      });

      // Discover services
      List<BluetoothService> services = await device.discoverServices();
      for (var s in services) {
        if (s.uuid.toString() == serviceUuid) {
          for (var c in s.characteristics) {
            if (c.uuid.toString() == charUuid) {
              provisionCharacteristic = c;
              break;
            }
          }
        }
      }
      
      notifyListeners();
      return provisionCharacteristic != null;
    } catch (e) {
      debugPrint("Connection error: $e");
      return false;
    }
  }

  void disconnect() {
    connectedDevice?.disconnect();
    connectedDevice = null;
    provisionCharacteristic = null;
    _connSub?.cancel();
    notifyListeners();
  }

  Future<bool> sendWifiCredentials(String ssid, String password) async {
    if (provisionCharacteristic == null) return false;
    
    final payload = jsonEncode({"ssid": ssid, "pass": password});
    try {
      await provisionCharacteristic!.write(utf8.encode(payload), withoutResponse: false);
      return true;
    } catch (e) {
      debugPrint("Write error: $e");
      return false;
    }
  }
}
