import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/mqtt_state.dart';

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  @override
  void initState() {
    super.initState();
    // Connect to MQTT automatically when entering dashboard
    WidgetsBinding.instance.addPostFrameCallback((_) {
      context.read<MqttState>().connect();
    });
  }

  @override
  Widget build(BuildContext context) {
    final mqttState = context.watch<MqttState>();

    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        title: const Text('Robot 2.1 Remote'),
        backgroundColor: Colors.transparent,
        elevation: 0,
        actions: [
          Icon(
            mqttState.isConnected ? Icons.cloud_done : Icons.cloud_off,
            color: mqttState.isConnected ? Colors.green : Colors.red,
          ),
          const SizedBox(width: 20),
        ],
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                'Emotions Engine',
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 20),
              
              Expanded(
                child: GridView.count(
                  crossAxisCount: 2,
                  mainAxisSpacing: 16,
                  crossAxisSpacing: 16,
                  children: [
                    _buildEmotionButton(context, 'Happy', Icons.sentiment_very_satisfied, Colors.green, 'happy'),
                    _buildEmotionButton(context, 'Angry', Icons.sentiment_very_dissatisfied, Colors.red, 'angry'),
                    _buildEmotionButton(context, 'Sad', Icons.sentiment_dissatisfied, Colors.blue, 'sad'),
                    _buildEmotionButton(context, 'Dizzy', Icons.auto_awesome, Colors.purple, 'dizzy'),
                    _buildEmotionButton(context, 'Panic', Icons.warning, Colors.orange, 'panic'),
                    _buildEmotionButton(context, 'Sleepy', Icons.snooze, Colors.indigo, 'sleepy'),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildEmotionButton(BuildContext context, String label, IconData icon, Color color, String command) {
    return Material(
      color: Colors.white.withValues(alpha: 0.1),
      borderRadius: BorderRadius.circular(24),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: () {
          context.read<MqttState>().sendEmotionCommand(command);
          
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text('Triggered $label Emotion'),
              backgroundColor: color,
              duration: const Duration(seconds: 1),
            ),
          );
        },
        child: Container(
          decoration: BoxDecoration(
            border: Border.all(color: color.withValues(alpha: 0.5), width: 2),
            borderRadius: BorderRadius.circular(24),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: 48, color: color),
              const SizedBox(height: 12),
              Text(
                label,
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 18,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
