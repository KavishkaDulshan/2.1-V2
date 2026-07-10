import 'dart:ui';
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/mqtt_state.dart';

class UtilitiesScreen extends StatefulWidget {
  const UtilitiesScreen({super.key});

  @override
  State<UtilitiesScreen> createState() => _UtilitiesScreenState();
}

class _UtilitiesScreenState extends State<UtilitiesScreen> {
  final TextEditingController _cityController = TextEditingController();
  
  Timer? _pomodoroTimer;
  int _timeRemaining = 0; // in seconds

  @override
  void dispose() {
    _pomodoroTimer?.cancel();
    _cityController.dispose();
    super.dispose();
  }

  void _startLocalTimer(int minutes) {
    _pomodoroTimer?.cancel();
    setState(() {
      _timeRemaining = minutes * 60;
    });
    
    _pomodoroTimer = Timer.periodic(const Duration(seconds: 1), (timer) {
      if (_timeRemaining > 0) {
        setState(() {
          _timeRemaining--;
        });
      } else {
        timer.cancel();
      }
    });
  }

  void _stopLocalTimer() {
    _pomodoroTimer?.cancel();
    setState(() {
      _timeRemaining = 0;
    });
  }

  Future<void> _selectAlarmTime(BuildContext context) async {
    final TimeOfDay? picked = await showTimePicker(
      context: context,
      initialTime: TimeOfDay.now(),
    );
    if (picked != null && context.mounted) {
      context.read<MqttState>().publish("robot21/commands/master", '{"alarm_h": ${picked.hour}, "alarm_m": ${picked.minute}}');
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Alarm set for ${picked.format(context)}')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: const BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [Color(0xFF0F172A), Color(0xFF1E1B4B)],
        ),
      ),
      child: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                'Utilities',
                style: TextStyle(fontSize: 28, fontWeight: FontWeight.bold, color: Colors.white, letterSpacing: 1.2),
              ),
              const SizedBox(height: 24),
              
              Expanded(
                child: ListView(
                  children: [
                    // --- CLOCK MODE ---
                    _buildGlassCard(
                      context,
                      title: 'Clock Mode',
                      icon: Icons.access_time,
                      child: Row(
                        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                        children: [
                          FilledButton.icon(
                            onPressed: () {
                              context.read<MqttState>().publish("robot21/commands/master", '{"mode": "clock"}');
                            },
                            icon: const Icon(Icons.alarm_on),
                            label: const Text('Show Clock'),
                            style: FilledButton.styleFrom(backgroundColor: Colors.blueAccent),
                          ),
                          OutlinedButton.icon(
                            onPressed: () {
                              context.read<MqttState>().publish("robot21/commands/master", '{"mode": "eyes"}');
                            },
                            icon: const Icon(Icons.face),
                            label: const Text('Hide Clock'),
                            style: OutlinedButton.styleFrom(foregroundColor: Colors.white),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 16),
                    
                    // --- POMODORO TIMER ---
                    _buildGlassCard(
                      context,
                      title: 'Pomodoro Timer',
                      icon: Icons.timer,
                      child: _timeRemaining > 0 
                      ? Column(
                          children: [
                            Text(
                              '${(_timeRemaining ~/ 60).toString().padLeft(2, '0')}:${(_timeRemaining % 60).toString().padLeft(2, '0')}',
                              style: const TextStyle(fontSize: 48, fontWeight: FontWeight.bold, color: Colors.greenAccent),
                            ),
                            const SizedBox(height: 8),
                            FilledButton.icon(
                              onPressed: () {
                                context.read<MqttState>().publish("robot21/commands/master", '{"timer": 0}');
                                _stopLocalTimer();
                              },
                              icon: const Icon(Icons.stop),
                              label: const Text('Cancel Timer'),
                              style: FilledButton.styleFrom(backgroundColor: Colors.redAccent),
                            ),
                          ],
                        )
                      : Row(
                        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                        children: [
                          FilledButton(
                            onPressed: () {
                              context.read<MqttState>().publish("robot21/commands/master", '{"timer": 25}');
                              _startLocalTimer(25);
                              ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Started 25m focus timer')));
                            },
                            style: FilledButton.styleFrom(backgroundColor: Colors.green),
                            child: const Text('25m Focus'),
                          ),
                          FilledButton(
                            onPressed: () {
                              context.read<MqttState>().publish("robot21/commands/master", '{"timer": 5}');
                              _startLocalTimer(5);
                              ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Started 5m break timer')));
                            },
                            style: FilledButton.styleFrom(backgroundColor: Colors.teal),
                            child: const Text('5m Break'),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 16),
                    
                    // --- SMART ALARM ---
                    _buildGlassCard(
                      context,
                      title: 'Smart Alarm',
                      icon: Icons.alarm_add,
                      child: Center(
                        child: FilledButton.icon(
                          onPressed: () => _selectAlarmTime(context),
                          icon: const Icon(Icons.access_alarm),
                          label: const Text('Set Alarm Time'),
                          style: FilledButton.styleFrom(backgroundColor: Colors.orangeAccent),
                        ),
                      ),
                    ),
                    const SizedBox(height: 16),
                    
                    // --- WEATHER ---
                    _buildGlassCard(
                      context,
                      title: 'Weather Location',
                      icon: Icons.cloud,
                      child: Row(
                        children: [
                          Expanded(
                            child: TextField(
                              controller: _cityController,
                              style: const TextStyle(color: Colors.white),
                              decoration: const InputDecoration(
                                hintText: 'e.g. London,UK',
                                hintStyle: TextStyle(color: Colors.white54),
                                enabledBorder: UnderlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
                                focusedBorder: UnderlineInputBorder(borderSide: BorderSide(color: Colors.blueAccent)),
                              ),
                            ),
                          ),
                          IconButton(
                            icon: const Icon(Icons.send, color: Colors.blueAccent),
                            onPressed: () {
                              if (_cityController.text.isNotEmpty) {
                                context.read<MqttState>().publish("robot21/commands/master", '{"city": "${_cityController.text}"}');
                                ScaffoldMessenger.of(context).showSnackBar(
                                  SnackBar(content: Text('Weather set to ${_cityController.text}')),
                                );
                              }
                            },
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildGlassCard(BuildContext context, {required String title, required IconData icon, required Widget child}) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(24),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 20, sigmaY: 20),
        child: Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: Colors.white.withValues(alpha: 0.05),
            borderRadius: BorderRadius.circular(24),
            border: Border.all(color: Colors.white.withValues(alpha: 0.1)),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(icon, color: Colors.white70),
                  const SizedBox(width: 12),
                  Text(title, style: const TextStyle(color: Colors.white, fontSize: 18, fontWeight: FontWeight.w600)),
                ],
              ),
              const SizedBox(height: 16),
              child,
            ],
          ),
        ),
      ),
    );
  }
}
