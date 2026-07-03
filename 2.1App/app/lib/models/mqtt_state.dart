// ignore_for_file: avoid_print
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

class MqttState extends ChangeNotifier {
  MqttServerClient? _client;
  bool _isConnected = false;

  bool get isConnected => _isConnected;

  Future<void> connect() async {
    // Connect to the same public broker
    _client = MqttServerClient('broker.emqx.io', 'flutter_client_${DateTime.now().millisecondsSinceEpoch}');
    _client!.port = 1883;
    _client!.logging(on: false);
    _client!.keepAlivePeriod = 20;
    _client!.onDisconnected = _onDisconnected;
    _client!.onConnected = _onConnected;

    final connMess = MqttConnectMessage()
        .withClientIdentifier(_client!.clientIdentifier)
        .withWillTopic('willtopic')
        .withWillMessage('My Will message')
        .startClean()
        .withWillQos(MqttQos.atLeastOnce);
    
    _client!.connectionMessage = connMess;

    try {
      print('Connecting to MQTT broker...');
      await _client!.connect();
    } catch (e) {
      print('Exception: $e');
      _client!.disconnect();
    }

    if (_client!.connectionStatus!.state == MqttConnectionState.connected) {
      print('Connected to MQTT broker!');
      _isConnected = true;
      notifyListeners();
    } else {
      print('Failed to connect to MQTT broker.');
      _client!.disconnect();
    }
  }

  void _onConnected() {
    print('Connected');
  }

  void _onDisconnected() {
    print('Disconnected');
    _isConnected = false;
    notifyListeners();
  }

  void sendEmotionCommand(String emotion) {
    if (_client == null || _client!.connectionStatus!.state != MqttConnectionState.connected) {
      print('Cannot send command, not connected');
      return;
    }

    final builder = MqttClientPayloadBuilder();
    // JSON payload that matches the C++ struct
    final payload = jsonEncode({'emotion': emotion});
    builder.addString(payload);

    _client!.publishMessage('robot21/commands/master', MqttQos.atLeastOnce, builder.payload!);
    print('Published: $payload');
  }
}
