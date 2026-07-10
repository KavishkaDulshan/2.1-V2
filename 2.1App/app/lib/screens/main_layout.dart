import 'package:flutter/material.dart';
import 'dashboard_screen.dart';
import 'utilities_screen.dart';

class MainLayout extends StatefulWidget {
  const MainLayout({super.key});

  @override
  State<MainLayout> createState() => _MainLayoutState();
}

class _MainLayoutState extends State<MainLayout> {
  int _currentIndex = 0;

  final List<Widget> _screens = [
    const DashboardScreen(),
    const UtilitiesScreen(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: _screens[_currentIndex],
      bottomNavigationBar: NavigationBar(
        backgroundColor: const Color(0xFF1E1B4B),
        indicatorColor: Colors.blueAccent.withValues(alpha: 0.3),
        selectedIndex: _currentIndex,
        onDestinationSelected: (index) {
          setState(() {
            _currentIndex = index;
          });
        },
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.face_outlined, color: Colors.white70),
            selectedIcon: Icon(Icons.face, color: Colors.blueAccent),
            label: 'Emotions',
          ),
          NavigationDestination(
            icon: Icon(Icons.grid_view_outlined, color: Colors.white70),
            selectedIcon: Icon(Icons.grid_view, color: Colors.blueAccent),
            label: 'Utilities',
          ),
        ],
      ),
    );
  }
}
