# Virtual Webcam Setup

Turn your webcam into an ASCII art camera. Glif processes each frame through its rendering pipeline and outputs to a virtual camera device that other apps (Zoom, Google Meet, OBS) can see.

## Output Modes

| Flag | Description | Use case |
|------|------------|----------|
| `--pipe-ppm` | PPM frames to stdout (with headers) | Pipe to `ffmpeg -f image2pipe` |
| `--pipe-raw` | Raw RGB24 to stdout (no headers) | Pipe to `ffmpeg -f rawvideo`, lower overhead |
| `--v4l2 <dev>` | Write directly to v4l2loopback device | Linux only, eliminates output ffmpeg process |

## Linux — v4l2loopback (recommended)

### Install v4l2loopback

```bash
# Debian/Ubuntu
sudo apt install v4l2loopback-dkms

# Arch
sudo pacman -S v4l2loopback-dkms

# Fedora
sudo dnf install v4l2loopback
```

### Create virtual camera

```bash
sudo modprobe v4l2loopback video_nr=10 card_label="Glif ASCII Cam" exclusive_caps=1
```

### Direct mode (recommended, 2 processes)

```bash
ffmpeg -f v4l2 -i /dev/video0 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf --v4l2 /dev/video10 --dark -s 2
```

### Pipe mode (fallback, 3 processes)

```bash
ffmpeg -f v4l2 -i /dev/video0 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf --pipe-raw --dark -s 2 | \
  ffmpeg -f rawvideo -pix_fmt rgb24 -video_size 1280x960 -framerate 30 -i - \
    -vf format=yuv420p -f v4l2 /dev/video10
```

### One-liner with script

```bash
./scripts/glif-webcam.sh
./scripts/glif-webcam.sh --src /dev/video2 --dst /dev/video10 --scale 3
```

### Use it

Open Zoom / Google Meet / Discord — select "Glif ASCII Cam" as your camera.

## macOS — ffmpeg + OBS Virtual Camera

macOS doesn't have v4l2loopback. Use OBS as a virtual camera bridge:

1. Install OBS Studio and enable its Virtual Camera
2. Create a named pipe and feed glif output through it:

```bash
mkfifo /tmp/glif_cam
ffmpeg -f avfoundation -i "0" -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf --pipe-ppm --dark -s 2 | \
  ffmpeg -f image2pipe -framerate 30 -i - -f mpegts /tmp/glif_cam
```

3. In OBS: add a "Media Source" pointing to `/tmp/glif_cam`, then Start Virtual Camera

Or use the web demo (works everywhere):

```bash
make wasm && cd web && python3 -m http.server 8000
```

Open in browser, click Webcam, share the browser tab.

## Windows — ffmpeg + OBS Virtual Camera

```bash
ffmpeg -f dshow -i video="Webcam Name" -f rawvideo -pix_fmt rgb24 -s 640x480 - | ^
  glif.exe --video 640 480 -f fonts/SFNSMono.ttf --pipe-ppm --dark -s 2 | ^
  ffmpeg -f image2pipe -framerate 30 -i - -f mpegts udp://localhost:1234
```

In OBS: add Media Source with `udp://localhost:1234`, then Start Virtual Camera.

Or use the web demo (works on all platforms).

## Troubleshooting

**"Cannot open v4l2 device"** — check that `v4l2loopback` is loaded (`lsmod | grep v4l2loopback`) and the device exists (`ls /dev/video*`).

**Low FPS** — reduce resolution (`--width 320 --height 240`) or scale (`-s 1`). The pipeline is fast (~1ms/frame), but ffmpeg decode/encode and v4l2 writes add overhead.

**Black output** — make sure `exclusive_caps=1` is set when loading v4l2loopback. Some apps won't show the feed without it.
