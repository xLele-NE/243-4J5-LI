#!/usr/bin/env python3
import serial
import sys
import time

try:
    ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
    print("Connexion au port série /dev/ttyACM0 à 115200 baud...")
    print("Appuyez sur Ctrl+C pour arrêter\n")
    print("="*60)

    time.sleep(2)  # Attendre un peu pour stabiliser la connexion

    # Lire pendant 60 secondes
    start_time = time.time()
    while time.time() - start_time < 60:
        if ser.in_waiting > 0:
            try:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    print(line)
            except Exception as e:
                print(f"Erreur de lecture: {e}")
        time.sleep(0.01)

    print("\n" + "="*60)
    print("Fin du monitoring (timeout 60s)")
    ser.close()

except serial.SerialException as e:
    print(f"Erreur: {e}")
    sys.exit(1)
except KeyboardInterrupt:
    print("\nInterrompu par l'utilisateur")
    if 'ser' in locals():
        ser.close()
    sys.exit(0)
