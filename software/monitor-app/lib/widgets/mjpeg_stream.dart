import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';

class MjpegStream extends StatefulWidget {
  final String url;
  final bool connected;
  final VoidCallback? onDisconnected;

  const MjpegStream({
    super.key,
    required this.url,
    required this.connected,
    this.onDisconnected,
  });

  @override
  State<MjpegStream> createState() => _MjpegStreamState();
}

class _MjpegStreamState extends State<MjpegStream> {
  Uint8List? _currentFrame;
  String _status = 'Disconnected';
  bool _running = false;
  StreamSubscription<List<int>>? _subscription;
  HttpClient? _httpClient;

  @override
  void didUpdateWidget(MjpegStream old) {
    super.didUpdateWidget(old);
    if (widget.url != old.url || widget.connected != old.connected) {
      _stop();
      if (widget.connected) {
        _start();
      }
    }
  }

  @override
  void initState() {
    super.initState();
    if (widget.connected) {
      _start();
    }
  }

  @override
  void dispose() {
    _stop();
    super.dispose();
  }

  void _start() {
    if (_running) return;
    _running = true;
    _status = 'Connecting...';
    _httpClient = HttpClient();
    _httpClient!.connectionTimeout = const Duration(seconds: 5);

    if (kDebugMode) {
      print('MjpegStream: Starting connection to ${widget.url}');
    }
    _connect().catchError((_) {});
  }

  Future<void> _connect() async {
    if (!_running) return;

    try {
      final uri = Uri.parse(widget.url);
      if (kDebugMode) {
        print('MjpegStream: Connecting to $uri');
      }
      final request = await _httpClient!.getUrl(uri);
      if (kDebugMode) {
        print('MjpegStream: Request sent, waiting for response...');
      }
      final response = await request.close();

      if (kDebugMode) {
        print('MjpegStream: Response received, status: ${response.statusCode}');
      }

      if (response.statusCode != 200) {
        _onError('HTTP ${response.statusCode}');
        return;
      }

      if (mounted) {
        setState(() => _status = 'Connected');
      }

      // JPEG SOI (0xFFD8) and EOI (0xFFD9) markers
      final data = BytesBuilder();
      bool inFrame = false;

      _subscription = response.listen(
        (chunk) {
          if (!_running) return;
          data.add(chunk);

          final bytes = data.toBytes();
          data.clear();

          int i = 0;
          while (i < bytes.length - 1) {
            if (!inFrame) {
              // Look for SOI
              if (bytes[i] == 0xFF && bytes[i + 1] == 0xD8) {
                inFrame = true;
                data.addByte(0xFF);
                data.addByte(0xD8);
                i += 2;
                continue;
              }
              i++;
            } else {
              // Copy bytes, look for EOI
              data.addByte(bytes[i]);
              if (bytes[i] == 0xFF && i + 1 < bytes.length && bytes[i + 1] == 0xD9) {
                data.addByte(0xD9);
                final frame = data.toBytes();
                data.clear();
                inFrame = false;
                // Only keep latest frame
                if (mounted) {
                  setState(() => _currentFrame = Uint8List.fromList(frame));
                }
                i += 2;
                continue;
              }
              i++;
            }
          }
          // Handle remaining byte
          if (i < bytes.length && inFrame) {
            data.addByte(bytes[i]);
          }
        },
        onDone: () {
          _onError('Stream closed');
        },
        onError: (e) {
          _onError('Stream error: $e');
        },
        cancelOnError: false,
      );
    } catch (e) {
      _onError('Connection failed: $e');
    }
  }

  void _onError(String msg) {
    if (!mounted || !_running) return;
    if (kDebugMode) {
      print('MjpegStream: Error - $msg');
    }
    setState(() => _status = msg);
    _running = false;
    widget.onDisconnected?.call();
    // Auto-reconnect after 2 seconds
    Future.delayed(const Duration(seconds: 2), () {
      if (mounted && widget.connected) {
        _start();
      }
    });
  }

  void _stop() {
    _running = false;
    _subscription?.cancel();
    _subscription = null;
    _httpClient?.close(force: true);
    _httpClient = null;
    if (mounted) {
      setState(() {
        _status = 'Disconnected';
        _currentFrame = null;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_currentFrame != null) {
      return Image.memory(
        _currentFrame!,
        gaplessPlayback: true,
        fit: BoxFit.contain,
      );
    }

    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.videocam_off,
              size: 64, color: Colors.grey[400]),
          const SizedBox(height: 16),
          Text(_status,
              style: TextStyle(color: Colors.grey[600], fontSize: 16)),
        ],
      ),
    );
  }
}
