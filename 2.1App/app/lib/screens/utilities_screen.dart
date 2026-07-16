import 'dart:ui';
import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:http/http.dart' as http;
import '../models/mqtt_state.dart';
import 'package:flutter_notification_listener/flutter_notification_listener.dart';

class UtilitiesScreen extends StatefulWidget {
  const UtilitiesScreen({super.key});

  @override
  State<UtilitiesScreen> createState() => _UtilitiesScreenState();
}

class _UtilitiesScreenState extends State<UtilitiesScreen> {
  final TextEditingController _cityController = TextEditingController();
  
  Timer? _pomodoroTimer;
  int _timeRemaining = 0; // in seconds
  bool _isClockMode = false;

  bool _sbEnable = false;
  bool _sbWifi = false;
  bool _sbTime = false;

  bool _notifMaster = false;
  final Map<String, bool> _notifApps = {
    'whatsapp': false,
    'telegram': false,
    'sms': false,
    'phone': false,
    'gmail': false,
    'youtube': false,
    'facebook': false,
    'instagram': false,
    'tiktok': false,
  };

  @override
  void initState() {
    super.initState();
    _loadSavedCity();
    _loadToggles();
  }

  Future<void> _loadToggles() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _notifMaster = prefs.getBool('notif_master') ?? false;
      _sbEnable = prefs.getBool('sb_en') ?? false;
      _sbWifi = prefs.getBool('sb_wifi') ?? false;
      _sbTime = prefs.getBool('sb_time') ?? false;
      for (final app in _notifApps.keys.toList()) {
        _notifApps[app] = prefs.getBool('notif_app_$app') ?? false;
      }
    });
  }

  Future<void> _saveToggle(String key, bool value) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(key, value);
  }

  Future<void> _loadSavedCity() async {
    final prefs = await SharedPreferences.getInstance();
    final savedCity = prefs.getString('weather_city');
    if (savedCity != null && savedCity.isNotEmpty) {
      setState(() {
        _cityController.text = savedCity;
      });
      // Delay MQTT publish slightly to ensure MqttState is fully ready
      Future.delayed(const Duration(seconds: 1), () {
        if (mounted) {
          context.read<MqttState>().publish("robot21/commands/master", '{"city": "$savedCity"}');
        }
      });
    }
  }

  Future<Iterable<String>> _searchCities(String query) async {
    if (query.length < 2) return const Iterable<String>.empty();
    try {
      final response = await http.get(Uri.parse(
          'http://api.openweathermap.org/geo/1.0/direct?q=$query&limit=5&appid=90b5371f426c8985201312f80bfe9eb4'));
      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        return data.map((item) {
          return '${item['name']},${item['country']}';
        });
      }
    } catch (e) {
      debugPrint("City search error: $e");
    }
    return const Iterable<String>.empty();
  }

  Future<void> _saveAndSendCity(String city) async {
    if (city.isEmpty) return;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('weather_city', city);
    if (mounted) {
      context.read<MqttState>().publish("robot21/commands/master", '{"city": "$city"}');
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Weather set to $city')),
      );
    }
  }

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

  Future<void> _showCustomTimerDialog(BuildContext context) async {
    final TextEditingController customTimeController = TextEditingController();
    return showDialog(
      context: context,
      builder: (context) {
        return AlertDialog(
          backgroundColor: const Color(0xFF1E1B4B),
          title: const Text('Custom Timer', style: TextStyle(color: Colors.white)),
          content: TextField(
            controller: customTimeController,
            keyboardType: TextInputType.number,
            style: const TextStyle(color: Colors.white),
            decoration: const InputDecoration(
              hintText: "Enter minutes",
              hintStyle: TextStyle(color: Colors.white54),
              enabledBorder: UnderlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
              focusedBorder: UnderlineInputBorder(borderSide: BorderSide(color: Colors.blueAccent)),
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Cancel', style: TextStyle(color: Colors.white70)),
            ),
            FilledButton(
              onPressed: () {
                final int? min = int.tryParse(customTimeController.text);
                if (min != null && min > 0) {
                  if (mounted) {
                    context.read<MqttState>().publish("robot21/commands/master", '{"timer": $min}');
                    _startLocalTimer(min);
                    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Started ${min}m timer')));
                  }
                }
                Navigator.pop(context);
              },
              style: FilledButton.styleFrom(backgroundColor: Colors.blueAccent),
              child: const Text('Start'),
            ),
          ],
        );
      },
    );
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
                      child: SwitchListTile(
                        title: const Text('Show Clock on Robot', style: TextStyle(color: Colors.white)),
                        value: _isClockMode,
                        activeThumbColor: Colors.blueAccent,
                        contentPadding: EdgeInsets.zero,
                        onChanged: (bool value) {
                          setState(() {
                            _isClockMode = value;
                          });
                          final mode = value ? "clock" : "eyes";
                          context.read<MqttState>().publish("robot21/commands/master", '{"mode": "$mode"}');
                        },
                      ),
                    ),
                    const SizedBox(height: 16),
                    
                    // --- STATUS BAR ---
                    _buildGlassCard(
                      context,
                      title: 'Status Bar',
                      icon: Icons.horizontal_split,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          SwitchListTile(
                            contentPadding: EdgeInsets.zero,
                            title: const Text('Enable Status Bar', style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold)),
                            activeThumbColor: Colors.blueAccent,
                            value: _sbEnable,
                            onChanged: (val) {
                              setState(() => _sbEnable = val);
                              _saveToggle('sb_en', val);
                              context.read<MqttState>().publish("robot21/commands/master", '{"sb_en": $val}');
                            },
                          ),
                          if (_sbEnable) ...[
                            SwitchListTile(
                              contentPadding: const EdgeInsets.only(left: 16.0),
                              title: const Text('Show WiFi Icon', style: TextStyle(color: Colors.white)),
                              activeThumbColor: Colors.blueAccent,
                              value: _sbWifi,
                              onChanged: (val) {
                                setState(() => _sbWifi = val);
                                _saveToggle('sb_wifi', val);
                                context.read<MqttState>().publish("robot21/commands/master", '{"sb_wifi": $val}');
                              },
                            ),
                            SwitchListTile(
                              contentPadding: const EdgeInsets.only(left: 16.0),
                              title: const Text('Show Time', style: TextStyle(color: Colors.white)),
                              activeThumbColor: Colors.blueAccent,
                              value: _sbTime,
                              onChanged: (val) {
                                setState(() => _sbTime = val);
                                _saveToggle('sb_time', val);
                                context.read<MqttState>().publish("robot21/commands/master", '{"sb_time": $val}');
                              },
                            ),
                          ],
                        ],
                      ),
                    ),
                    const SizedBox(height: 16),

                    // --- NOTIFICATION MIRRORING ---
                    _buildGlassCard(
                      context,
                      title: 'Notification Mirroring',
                      icon: Icons.notifications_active,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          FutureBuilder<bool?>(
                            future: NotificationsListener.hasPermission,
                            builder: (context, snapshot) {
                              final hasPermission = snapshot.data ?? false;
                              return ListTile(
                                contentPadding: EdgeInsets.zero,
                                title: const Text('Android Notification Access', style: TextStyle(color: Colors.white)),
                                subtitle: Text(
                                  hasPermission ? 'Access Granted. Active in background.' : 'Tap to enable Android notification access',
                                  style: TextStyle(color: hasPermission ? Colors.greenAccent : Colors.white54),
                                ),
                                trailing: Icon(
                                  hasPermission ? Icons.check_circle : Icons.warning,
                                  color: hasPermission ? Colors.greenAccent : Colors.orangeAccent,
                                ),
                                onTap: () async {
                                  if (!hasPermission) {
                                    await NotificationsListener.openPermissionSettings();
                                  } else {
                                    bool isRunning = await NotificationsListener.isRunning ?? false;
                                    if (!isRunning) {
                                      await NotificationsListener.startService(foreground: true, title: 'Robot Notification Watcher');
                                    }
                                  }
                                },
                              );
                            },
                          ),
                          const Divider(color: Colors.white24),
                          SwitchListTile(
                            contentPadding: EdgeInsets.zero,
                            title: const Text('Enable Mirroring to Robot', style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold)),
                            activeThumbColor: Colors.greenAccent,
                            value: _notifMaster,
                            onChanged: (val) {
                              setState(() => _notifMaster = val);
                              _saveToggle('notif_master', val);
                            },
                          ),
                          if (_notifMaster) ...[
                            const SizedBox(height: 8),
                            const Text('Allowed Apps:', style: TextStyle(color: Colors.white70, fontSize: 13)),
                            const SizedBox(height: 4),
                            ..._notifApps.keys.map((app) {
                              String label = app[0].toUpperCase() + app.substring(1);
                              return SwitchListTile(
                                contentPadding: const EdgeInsets.only(left: 16.0),
                                title: Text(label, style: const TextStyle(color: Colors.white)),
                                activeThumbColor: Colors.greenAccent,
                                value: _notifApps[app]!,
                                onChanged: (val) {
                                  setState(() => _notifApps[app] = val);
                                  _saveToggle('notif_app_$app', val);
                                },
                              );
                            }),
                          ],
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
                          FilledButton.icon(
                            onPressed: () => _showCustomTimerDialog(context),
                            style: FilledButton.styleFrom(backgroundColor: Colors.blueAccent),
                            icon: const Icon(Icons.timer, size: 18),
                            label: const Text('Custom'),
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
                      child: Autocomplete<String>(
                        initialValue: TextEditingValue(text: _cityController.text),
                        optionsBuilder: (TextEditingValue textEditingValue) async {
                          if (textEditingValue.text.length < 2) return const Iterable<String>.empty();
                          return await _searchCities(textEditingValue.text);
                        },
                        onSelected: (String selection) {
                          _saveAndSendCity(selection);
                        },
                        fieldViewBuilder: (context, controller, focusNode, onFieldSubmitted) {
                          if (_cityController.text.isNotEmpty && controller.text.isEmpty) {
                            controller.text = _cityController.text;
                          }
                          return TextField(
                            controller: controller,
                            focusNode: focusNode,
                            style: const TextStyle(color: Colors.white),
                            decoration: InputDecoration(
                              hintText: 'Search city...',
                              hintStyle: const TextStyle(color: Colors.white54),
                              enabledBorder: const UnderlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
                              focusedBorder: const UnderlineInputBorder(borderSide: BorderSide(color: Colors.blueAccent)),
                              suffixIcon: IconButton(
                                icon: const Icon(Icons.send, color: Colors.blueAccent),
                                onPressed: () {
                                  _saveAndSendCity(controller.text);
                                },
                              ),
                            ),
                            onSubmitted: (String value) {
                              _saveAndSendCity(value);
                            },
                          );
                        },
                        optionsViewBuilder: (context, onSelected, options) {
                          return Align(
                            alignment: Alignment.topLeft,
                            child: Material(
                              color: const Color(0xFF1E1B4B),
                              elevation: 4.0,
                              borderRadius: BorderRadius.circular(8),
                              child: SizedBox(
                                width: 250,
                                child: ListView.builder(
                                  padding: EdgeInsets.zero,
                                  shrinkWrap: true,
                                  itemCount: options.length,
                                  itemBuilder: (BuildContext context, int index) {
                                    final String option = options.elementAt(index);
                                    return ListTile(
                                      title: Text(option, style: const TextStyle(color: Colors.white)),
                                      onTap: () {
                                        onSelected(option);
                                      },
                                    );
                                  },
                                ),
                              ),
                            ),
                          );
                        },
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
