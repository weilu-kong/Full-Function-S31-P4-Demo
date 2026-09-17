#!/usr/bin/env python3
"""
Real-time Voice Recognition Monitor for ESP32-S31 Korvo-1.
Captures WakeNet and MultiNet diagnostic logs to evaluate Japanese speech accuracy.
Simultaneously outputs to terminal, saves to test/voice_test_live.log,
AND automatically captures dumped speech audio into test/recordings/*.wav!
"""

import sys
import os
import time
import base64
import wave
import serial

PORT = "/dev/cu.usbserial-1120"
BAUD = 115200

# Directory and files
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_FILE = os.path.join(SCRIPT_DIR, "voice_test_live.log")
REC_DIR = os.path.join(SCRIPT_DIR, "recordings")

def main():
    os.makedirs(REC_DIR, exist_ok=True)
    print(f"Opening {PORT} at {BAUD} baud...")
    print(f"Logging test output to: {LOG_FILE}")
    print(f"Audio recordings saved to: {REC_DIR}/")
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
    except Exception as e:
        print(f"Failed to open port {PORT}: {e}")
        return 1

    log_fp = open(LOG_FILE, "a", encoding="utf-8")
    init_msg = f"\n=== Voice Test Session Started at {time.strftime('%Y-%m-%d %H:%M:%S')} ===\n"
    log_fp.write(init_msg)
    log_fp.flush()

    print("=" * 65)
    print("ESP32-S31 Korvo-1 Japanese Voice Test Monitor")
    print("Listening for [WAKE], [MN DETECTED], [MN TIMEOUT]...")
    print("Audio dump recorder active: failed commands saved to WAV")
    print("=" * 65)

    buffer = ""
    collecting_audio = False
    audio_pcm = bytearray()
    audio_meta = {"samples": 0, "rate": 16000}

    try:
        while True:
            chunk = ser.read(ser.in_waiting or 1)
            if not chunk:
                time.sleep(0.02)
                continue
            text = chunk.decode("utf-8", errors="replace")
            buffer += text
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                # Handle audio dump stream
                if "[AUDIO_DUMP_START" in line:
                    collecting_audio = True
                    audio_pcm = bytearray()
                    print(f"\n📥 \033[94mReceiving speech audio dump from ESP32...\033[0m")
                    continue
                elif "[AUDIO_DUMP_END]" in line:
                    collecting_audio = False
                    if len(audio_pcm) > 0:
                        timestamp = time.strftime('%Y%m%d_%H%M%S')
                        wav_filename = f"failed_speech_{timestamp}.wav"
                        wav_path = os.path.join(REC_DIR, wav_filename)
                        try:
                            with wave.open(wav_path, "wb") as wf:
                                wf.setnchannels(1)
                                wf.setsampwidth(2)
                                wf.setframerate(16000)
                                wf.writeframes(audio_pcm)
                            duration_s = len(audio_pcm) / (16000 * 2)
                            print(f"🎙️  \033[95m[AUDIO SAVED] {duration_s:.2f}s speech saved to: test/recordings/{wav_filename}\033[0m\n")
                            log_fp.write(f"[AUDIO RECORDED] {wav_path} ({duration_s:.2f}s)\n")
                            log_fp.flush()
                        except Exception as err:
                            print(f"❌ Failed to save WAV: {err}")
                    continue
                elif collecting_audio and "[AUDIO_B64]" in line:
                    try:
                        b64_str = line.split("[AUDIO_B64]", 1)[1].strip()
                        audio_pcm.extend(base64.b64decode(b64_str))
                    except Exception:
                        pass
                    continue

                # Normal logging
                is_voice_log = any(k in line for k in [
                    "[WAKE]", "[MN DETECTED]", "[MN TIMEOUT]",
                    "voice_service:", "voice_svc:", "MultiNet",
                    "esp_codec_dev", "ASRC", "Dropped",
                    "JA_REG", "threshold verified", "ACTIVE SPEECH COMMANDS"
                ])

                if is_voice_log:
                    log_fp.write(line + "\n")
                    log_fp.flush()

                # Color-highlighted output for terminal
                if "[WAKE]" in line:
                    print(f"\n🔔 \033[92m{line}\033[0m")
                elif "[MN DETECTED]" in line:
                    print(f"🎯 \033[96m{line}\033[0m")
                elif "[MN TIMEOUT]" in line:
                    print(f"⚠️  \033[93m{line}\033[0m")
                elif "JA_REG" in line:
                    print(f"🔤 \033[90m{line}\033[0m")
                elif "voice_service:" in line or "voice_svc:" in line:
                    print(f"ℹ️  {line}")
                elif "esp_codec_dev" in line or "ASRC" in line or "Dropped" in line:
                    print(f"❌ \033[91m{line}\033[0m")
    except KeyboardInterrupt:
        print(f"\nMonitor stopped. Session logs saved to: {LOG_FILE}")
    finally:
        log_fp.write(f"=== Voice Test Session Ended at {time.strftime('%Y-%m-%d %H:%M:%S')} ===\n\n")
        log_fp.close()
        ser.close()

if __name__ == "__main__":
    sys.exit(main())
