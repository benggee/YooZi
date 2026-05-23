import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../widgets/mjpeg_stream.dart';

class MonitorScreen extends StatefulWidget {
  const MonitorScreen({super.key});

  @override
  State<MonitorScreen> createState() => _MonitorScreenState();
}

class _MonitorScreenState extends State<MonitorScreen> {
  final _ipController = TextEditingController(text: '');
  final _portController = TextEditingController(text: '8080');
  bool _connected = false;
  bool _fullscreen = false;

  String get _streamUrl {
    final ip = _ipController.text.trim();
    final port = _portController.text.trim();
    return 'http://$ip:$port/stream';
  }

  void _toggleConnection() {
    setState(() {
      _connected = !_connected;
    });
  }

  void _toggleFullscreen() {
    setState(() {
      _fullscreen = !_fullscreen;
    });
    if (_fullscreen) {
      SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
    } else {
      SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
    }
  }

  @override
  void dispose() {
    _ipController.dispose();
    _portController.dispose();
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_fullscreen && _connected) {
      return _buildFullscreen();
    }
    return _buildNormal();
  }

  Widget _buildFullscreen() {
    return Scaffold(
      backgroundColor: Colors.black,
      body: GestureDetector(
        onTap: _toggleFullscreen,
        child: Center(
          child: AspectRatio(
            aspectRatio: 4 / 3,
            child: MjpegStream(
              url: _streamUrl,
              connected: _connected,
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildNormal() {
    return Scaffold(
      appBar: AppBar(
        title: const Text('YooZi Monitor'),
        actions: [
          if (_connected)
            IconButton(
              icon: const Icon(Icons.fullscreen),
              onPressed: _toggleFullscreen,
              tooltip: 'Fullscreen',
            ),
        ],
      ),
      body: Column(
        children: [
          // Video area
          Expanded(
            child: Container(
              color: Colors.black,
              child: _connected
                  ? MjpegStream(
                      url: _streamUrl,
                      connected: _connected,
                      onDisconnected: () {
                        if (mounted) setState(() => _connected = false);
                      },
                    )
                  : const Center(
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(Icons.videocam_off,
                              size: 64, color: Colors.grey),
                          SizedBox(height: 16),
                          Text('Not connected',
                              style:
                                  TextStyle(color: Colors.grey, fontSize: 16)),
                        ],
                      ),
                    ),
            ),
          ),

          // Controls
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: Theme.of(context).colorScheme.surface,
              border: Border(
                top: BorderSide(
                  color: Theme.of(context).dividerColor,
                ),
              ),
            ),
            child: Column(
              children: [
                Row(
                  children: [
                    Expanded(
                      flex: 3,
                      child: TextField(
                        controller: _ipController,
                        enabled: !_connected,
                        decoration: const InputDecoration(
                          labelText: 'IP Address',
                          hintText: '192.168.1.100',
                          isDense: true,
                          border: OutlineInputBorder(),
                        ),
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      flex: 1,
                      child: TextField(
                        controller: _portController,
                        enabled: !_connected,
                        keyboardType: TextInputType.number,
                        decoration: const InputDecoration(
                          labelText: 'Port',
                          isDense: true,
                          border: OutlineInputBorder(),
                        ),
                      ),
                    ),
                    const SizedBox(width: 12),
                    FilledButton.icon(
                      onPressed: _ipController.text.isEmpty && !_connected
                          ? null
                          : _toggleConnection,
                      icon: Icon(_connected ? Icons.stop : Icons.play_arrow),
                      label: Text(_connected ? 'Stop' : 'Connect'),
                    ),
                  ],
                ),
                if (_connected) ...[
                  const SizedBox(height: 8),
                  Row(
                    children: [
                      Container(
                        width: 8,
                        height: 8,
                        decoration: const BoxDecoration(
                          color: Colors.green,
                          shape: BoxShape.circle,
                        ),
                      ),
                      const SizedBox(width: 8),
                      Text(
                        'Connected to ${_ipController.text}:${_portController.text}',
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                    ],
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}
