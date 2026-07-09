import 'package:shared_preferences/shared_preferences.dart';

class PreferencesService {
  static const String _keyMacAddress = 'robot_mac_address';
  static const String _keyWifiSsid = 'wifi_ssid';
  static const String _keyWifiPass = 'wifi_password';

  static Future<void> saveRobotPairing(String macAddress, String ssid, String password) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_keyMacAddress, macAddress);
    await prefs.setString(_keyWifiSsid, ssid);
    await prefs.setString(_keyWifiPass, password);
  }

  static Future<void> clearRobotPairing() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_keyMacAddress);
    await prefs.remove(_keyWifiSsid);
    await prefs.remove(_keyWifiPass);
  }

  static Future<Map<String, String?>> getRobotPairing() async {
    final prefs = await SharedPreferences.getInstance();
    return {
      'mac': prefs.getString(_keyMacAddress),
      'ssid': prefs.getString(_keyWifiSsid),
      'pass': prefs.getString(_keyWifiPass),
    };
  }

  static Future<bool> isRobotPaired() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.containsKey(_keyMacAddress);
  }
}
