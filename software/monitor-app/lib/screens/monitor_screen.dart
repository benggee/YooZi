import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';
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
  String _streamStatus = 'Disconnected';

  String get _streamUrl {
    final ip = _ipController.text.trim();
    final port = _portController.text.trim();
    return 'http://$ip:$port/stream';
  }

  void _toggleConnection() {
    final ip = _ipController.text.trim();
    final port = _portController.text.trim();

    if (ip.isEmpty || port.isEmpty) {
      if (kDebugMode) {
        print('MonitorScreen: Cannot connect - IP: "$ip", Port: "$port"');
      }
      return;
    }

    setState(() {
      _connected = !_connected;
      if (!_connected) {
        _streamStatus = 'Disconnected';
      }
    });

    if (kDebugMode) {
      print('MonitorScreen: Connection toggled, connected: $_connected');
      print('MonitorScreen: Stream URL: $_streamUrl');
    }
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
  void initState() {
    super.initState();
    _ipController.addListener(_onTextChanged);
    _portController.addListener(_onTextChanged);
  }

  void _onTextChanged() {
    setState(() {});
  }

  @override
  void dispose() {
    _ipController.removeListener(_onTextChanged);
    _portController.removeListener(_onTextChanged);
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
              onStatusChanged: (status) {
                if (mounted) setState(() => _streamStatus = status);
              },
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildNormal() {
    return Scaffold(
      appBar: AppBar(
        title: const Text('YooZi See'),
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
                      onStatusChanged: (status) {
                        if (mounted) setState(() => _streamStatus = status);
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
                      onPressed: (_ipController.text.trim().isEmpty ||
                                  _portController.text.trim().isEmpty) &&
                              !_connected
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
                        decoration: BoxDecoration(
                          color: _streamStatus == 'Connected'
                              ? Colors.green
                              : Colors.orange,
                          shape: BoxShape.circle,
                        ),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          _streamStatus == 'Connected'
                              ? 'Connected to ${_ipController.text}:${_portController.text}'
                              : _streamStatus,
                          style: Theme.of(context).textTheme.bodySmall,
                          overflow: TextOverflow.ellipsis,
                        ),
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
