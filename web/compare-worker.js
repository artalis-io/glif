/*
 * compare-worker.js — Web Worker for ffmpeg-wasm video frame decoding.
 *
 * Decodes video into raw RGBA frames and posts them back as Transferable
 * ArrayBuffers for zero-copy transfer to the main thread.
 *
 * Messages IN:
 *   { type: 'init', videoData: ArrayBuffer }
 *   { type: 'decode', startTime: float, numFrames: int, width: int, height: int }
 *   { type: 'probe' }
 *
 * Messages OUT:
 *   { type: 'ready', duration: float, width: int, height: int, fps: float }
 *   { type: 'frames', startTime: float, frames: ArrayBuffer[], width: int, height: int }
 *   { type: 'error', message: string }
 */

let ffmpeg = null;
let videoFileName = 'input.mp4';
let probeInfo = null;

async function initFFmpeg() {
    const { FFmpeg } = await import('../vendor/ffmpeg/ffmpeg.js');
    ffmpeg = new FFmpeg();
    await ffmpeg.load({
        coreURL: '../vendor/ffmpeg/ffmpeg-core.js',
        wasmURL: '../vendor/ffmpeg/ffmpeg-core.wasm',
    });
}

self.onmessage = async function(e) {
    const msg = e.data;

    try {
        switch (msg.type) {
            case 'init': {
                await initFFmpeg();
                const data = new Uint8Array(msg.videoData);
                await ffmpeg.writeFile(videoFileName, data);

                // Probe video metadata
                await ffmpeg.exec(['-i', videoFileName, '-f', 'null', '-']);
                // ffmpeg logs metadata to stderr which we can't easily capture
                // in this version. Caller provides dimensions from their own source.
                self.postMessage({ type: 'ready' });
                break;
            }

            case 'probe': {
                // Probe by decoding first frame to get actual dimensions
                await ffmpeg.exec([
                    '-i', videoFileName,
                    '-frames:v', '1',
                    '-f', 'rawvideo',
                    '-pix_fmt', 'rgba',
                    'probe_frame.raw'
                ]);
                const probeData = await ffmpeg.readFile('probe_frame.raw');
                await ffmpeg.deleteFile('probe_frame.raw');

                // We need dimensions from the caller since rawvideo has no header
                self.postMessage({
                    type: 'probe_done',
                    frameBytes: probeData.length
                });
                break;
            }

            case 'decode': {
                const { startTime, numFrames, width, height } = msg;
                const outFile = 'chunk.raw';

                const args = [
                    '-ss', String(startTime),
                    '-i', videoFileName,
                    '-frames:v', String(numFrames),
                    '-f', 'rawvideo',
                    '-pix_fmt', 'rgba',
                    '-s', width + 'x' + height,
                    outFile
                ];

                await ffmpeg.exec(args);

                let rawData;
                try {
                    rawData = await ffmpeg.readFile(outFile);
                    await ffmpeg.deleteFile(outFile);
                } catch (err) {
                    self.postMessage({
                        type: 'error',
                        message: 'No frames decoded at t=' + startTime
                    });
                    break;
                }

                const frameSize = width * height * 4;
                const actualFrames = Math.floor(rawData.length / frameSize);
                const frames = [];

                for (let i = 0; i < actualFrames; i++) {
                    const buf = rawData.buffer.slice(
                        rawData.byteOffset + i * frameSize,
                        rawData.byteOffset + (i + 1) * frameSize
                    );
                    frames.push(buf);
                }

                self.postMessage(
                    { type: 'frames', startTime, frames, width, height },
                    frames // Transfer ownership
                );
                break;
            }
        }
    } catch (err) {
        self.postMessage({ type: 'error', message: err.message });
    }
};
