#!/usr/bin/env python3
"""
Interface tactile pour Raspberry Pi - Framebuffer direct avec MMAP et correction STRIDE
"""
import os
import sys
import json
import time
import struct
import fcntl
import mmap
import threading
import paho.mqtt.client as mqtt
import ssl
import evdev

print("--- DEMARRAGE DU SCRIPT ---")

sys.path.append('/home/xlele/243-4J5-LI/labo/Labo-02/led-control')
try:
    from mqtt_config import MQTT_CONFIG
except:
    MQTT_CONFIG = {
        "broker": "mqtt.xlele.ca", "port": 443,
        "username": "esp_user", "password": "Teladmin1$",
        "device_id": "lte-xlele"
    }

# --- Couleurs 32-bit BGRA ---
def bgra(r, g, b): return bytes([b, g, r, 0])
NOIR, BLANC = bgra(0,0,0), bgra(255,255,255)
BLEU_F, BLEU_FONCE = bgra(21,101,192), bgra(13,71,161)
ROUGE, VERT, VERT_CLAIR = bgra(211,47,47), bgra(46,125,50), bgra(76,175,80)
JAUNE, ORANGE = bgra(255,193,7), bgra(255,111,0)
GRIS, GRIS_CLAIR = bgra(117,117,117), bgra(189,189,189)
FOND, CARTE = bgra(26,26,46), bgra(22,33,62)

FONT = {
    'A': [14, 17, 17, 31, 17, 17, 17], 'B': [30, 17, 30, 17, 17, 30, 0], 'C': [14, 17, 16, 16, 17, 14, 0],
    'D': [30, 17, 17, 17, 17, 30, 0], 'E': [31, 16, 30, 16, 16, 31, 0], 'F': [31, 16, 30, 16, 16, 16, 0],
    'G': [14, 17, 16, 19, 17, 14, 0], 'H': [17, 17, 31, 17, 17, 17, 0], 'I': [14, 4, 4, 4, 4, 14, 0],
    'J': [7, 2, 2, 2, 18, 12, 0], 'K': [17, 18, 28, 18, 17, 17, 0], 'L': [16, 16, 16, 16, 16, 31, 0],
    'M': [17, 27, 21, 17, 17, 17, 0], 'N': [17, 25, 21, 19, 17, 17, 0], 'O': [14, 17, 17, 17, 17, 14, 0],
    'P': [30, 17, 30, 16, 16, 16, 0], 'Q': [14, 17, 17, 21, 18, 13, 0], 'R': [30, 17, 30, 18, 17, 17, 0],
    'S': [14, 17, 14, 1, 17, 14, 0], 'T': [31, 4, 4, 4, 4, 4, 0], 'U': [17, 17, 17, 17, 17, 14, 0],
    'V': [17, 17, 17, 17, 10, 4, 0], 'W': [17, 17, 21, 21, 21, 10, 0], 'X': [17, 10, 4, 10, 17, 0, 0],
    'Y': [17, 10, 4, 4, 4, 4, 0], 'Z': [31, 2, 4, 8, 16, 31, 0], '0': [14, 17, 17, 17, 17, 14, 0],
    '1': [4, 12, 4, 4, 4, 14, 0], '2': [14, 17, 2, 4, 8, 31, 0], '3': [14, 17, 6, 1, 17, 14, 0],
    '4': [2, 6, 10, 31, 2, 2, 0], '5': [31, 16, 30, 1, 1, 30, 0], '6': [14, 16, 30, 17, 17, 14, 0],
    '7': [31, 1, 2, 4, 8, 8, 0], '8': [14, 17, 14, 17, 17, 14, 0], '9': [14, 17, 15, 1, 1, 14, 0],
    ' ': [0]*7, ':': [0, 4, 0, 4, 0, 0, 0], '.': [0, 0, 0, 0, 0, 12, 12], '-': [0, 0, 0, 31, 0, 0, 0],
    '!': [4, 4, 4, 4, 0, 4, 4], '=': [0, 31, 0, 31, 0, 0, 0], '+': [0, 4, 14, 4, 0, 0, 0],
    '>': [8, 4, 2, 4, 8, 0, 0], '<': [2, 4, 8, 4, 2, 0, 0], '/': [2, 4, 4, 8, 8, 16, 0],
    '(': [4, 8, 8, 8, 8, 4, 0], ')': [8, 4, 4, 4, 4, 8, 0], '%': [17, 1, 2, 4, 8, 16, 17]
}

class Framebuffer:
    def __init__(self, dev='/dev/fb0'):
        self.fd = os.open(dev, os.O_RDWR)
        # Var info
        vinfo = struct.unpack('8I12I', fcntl.ioctl(self.fd, 0x4600, b'\0'*80))
        self.w, self.h, self.bpp = vinfo[0], vinfo[1], vinfo[6]
        # Fix info
        finfo_data = fcntl.ioctl(self.fd, 0x4602, b'\0'*80)
        # On a 64-bit system, we need to be careful with long vs int
        # We only really need line_length (offset varies) and smem_len
        self.stride = struct.unpack_from('I', finfo_data, 48)[0]
        self.size = struct.unpack_from('I', finfo_data, 24)[0]
        print(f"FB: {self.w}x{self.h}, bpp={self.bpp}, stride={self.stride}, size={self.size}")
        self.mm = mmap.mmap(self.fd, self.size, mmap.MAP_SHARED, mmap.PROT_WRITE)
        self.bpp_b = self.bpp // 8


    def rect(self, x, y, w, h, c):
        x1, y1 = max(0, x), max(0, y)
        x2, y2 = min(self.w, x+w), min(self.h, y+h)
        if x1 >= x2 or y1 >= y2: return
        row_data = c * (x2 - x1)
        row_len = len(row_data)
        for cy in range(y1, y2):
            off = cy * self.stride + x1 * self.bpp_b
            self.mm[off:off+row_len] = row_data

    def fill(self, c):
        row = c * self.w
        for y in range(self.h):
            off = y * self.stride
            self.mm[off:off+len(row)] = row

    def text(self, x, y, s, c=BLANC, sz=1):
        for i, char in enumerate(s.upper()):
            if char not in FONT: continue
            glyph = FONT[char]
            for row in range(7):
                for col in range(5):
                    if glyph[row] & (1 << (4 - col)):
                        self.rect(x + i*6*sz + col*sz, y + row*sz, sz, sz, c)

    def text_c(self, s, y, c=BLANC, sz=1):
        self.text((self.w - len(s)*6*sz)//2, y, s, c, sz)

class MQTTListener:
    def __init__(self, cb):
        self.cb = cb
        self.connected = False
        threading.Thread(target=self._start, daemon=True).start()

    def _start(self):
        try:
            cid = f"fb-{int(time.time())}"
            self.cl = mqtt.Client(client_id=cid, transport="websockets")
            self.cl.tls_set(cert_reqs=ssl.CERT_NONE)
            self.cl.username_pw_set(MQTT_CONFIG["username"], MQTT_CONFIG["password"])
            self.cl.on_connect = self._on_c
            self.cl.on_message = self._msg
            self.cl.connect(MQTT_CONFIG["broker"], MQTT_CONFIG["port"], 60)
            self.cl.loop_forever()
        except Exception as e:
            print(f"MQTT Error: {e}")

    def _on_c(self, c, u, f, rc):
        if rc == 0:
            self.connected = True
            for t in ["hydro-limoilou/poste-02/telemetry/#", "hydro-limoilou/poste-02/status", "hydro-limoilou/poste-02/alarm/#"]:
                c.subscribe(t)

    def _msg(self, c, u, m):
        try:
            d = json.loads(m.payload.decode())
            t = m.topic
            if "water_level" in t: self.cb("water", d.get("value", 0))
            elif "light" in t: self.cb("light", d.get("value", 0))
            elif "status" in t: self.cb("rssi", d.get("value", -100))
            elif "alarm" in t and d.get("status") != "OK": self.cb("alarm", d.get("message", "Alarme"))
        except: pass

class App:
    def __init__(self):
        print("Initialisation App...")
        self.fb = Framebuffer()
        print("FB Initialise.")
        self.data = {"water":0, "light":0, "rssi":-100, "alarms":[]}
        self.pg = "t"
        print("Connexion MQTT...")
        self.mq = MQTTListener(self._mqtt)
        print("Initialisation Tactile...")
        self._touch_init()
        print("App prete.")

    def _touch_init(self):
        self.td = None
        for i in range(10):
            try:
                d = evdev.InputDevice(f"/dev/input/event{i}")
                if "touch" in d.name.lower() or "ft5406" in d.name.lower():
                    self.td = d; break
            except: pass
        self.tx_r = (0, 720); self.ty_r = (0, 1280)
        if self.td:
            try:
                ax = self.td.absinfo(evdev.ecodes.ABS_MT_POSITION_X)
                ay = self.td.absinfo(evdev.ecodes.ABS_MT_POSITION_Y)
                self.tx_r = (ax.min, ax.max)
                self.ty_r = (ay.min, ay.max)
            except: pass

    def _mqtt(self, t, v):
        if t == "alarm":
            if not any(a['m'] == v and not a['a'] for a in self.data["alarms"]):
                self.data["alarms"].insert(0, {'m': v, 't': time.strftime("%H:%M"), 'a': False})
        else: self.data[t] = v
        if t == "water" and v > 150: self._mqtt("alarm", f"EAU CRITIQUE: {v}cm")

    def draw(self):
        fb = self.fb
        fb.fill(FOND)
        # Header Nav
        bw = fb.w // 3
        for i, (l, k) in enumerate([("TELE", "t"), ("ALARM", "a"), ("LIEN", "l")]):
            fb.rect(i*bw, 0, bw, 80, BLEU_F if self.pg == k else BLEU_FONCE)
            fb.text_c(l, 30, BLANC, 2)
        
        if self.pg == "t":
            fb.text_c("TELEMETRIE", 110, BLANC, 3)
            fb.rect(20, 180, fb.w-40, 150, CARTE)
            fb.text(40, 200, "NIVEAU D'EAU", BLANC, 2)
            val = self.data["water"]
            c = ROUGE if val > 150 else VERT_CLAIR
            fb.text(40, 250, f"{val} CM", c, 4)
            fb.rect(20, 350, fb.w-40, 120, CARTE)
            fb.text(40, 370, "LUMINOSITE", BLANC, 2)
            fb.text(40, 410, f"{self.data['light']:.1f} LUX", JAUNE, 3)
        elif self.pg == "a":
            fb.text_c("ALARMES", 110, ROUGE, 3)
            y = 180
            active = [a for a in self.data["alarms"] if not a['a']]
            if not active: fb.text_c("RAS - AUCUNE ALARME", 300, VERT, 2)
            for a in active[:6]:
                fb.rect(10, y, fb.w-20, 90, ROUGE)
                fb.text(20, y+15, f"{a['t']} {a['m']}", BLANC, 1)
                fb.text(20, y+55, "TOUCHEZ POUR ACQUITTER", GRIS_CLAIR, 1)
                y += 100
        elif self.pg == "l":
            fb.text_c("LIAISON MQTT", 110, VERT, 3)
            fb.text(40, 180, f"SIGNAL: {self.data['rssi']} DBM", BLANC, 2)
            fb.text(40, 240, f"BROKER: {MQTT_CONFIG['broker']}", GRIS, 1)

        # Footer
        fb.rect(0, fb.h-50, fb.w, 50, NOIR)
        fb.text(20, fb.h-35, f"{time.strftime('%H:%M:%S')} | RSSI: {self.data['rssi']} dBm", GRIS_CLAIR, 1)

    def run(self):
        tx, ty = 0, 0
        while True:
            self.draw()
            if self.td:
                try:
                    for e in self.td.read():
                        if e.type == evdev.ecodes.EV_ABS:
                            if e.code == evdev.ecodes.ABS_MT_POSITION_X: tx = e.value
                            elif e.code == evdev.ecodes.ABS_MT_POSITION_Y: ty = e.value
                        elif e.type == evdev.ecodes.EV_KEY and e.code == evdev.ecodes.BTN_TOUCH and e.value == 1:
                            self._tap(tx, ty)
                except BlockingIOError: pass
            time.sleep(0.05)

    def _tap(self, tx, ty):
        # Conversion coordonnées tactiles -> écran
        x = int((tx - self.tx_r[0]) / max(1, self.tx_r[1] - self.tx_r[0]) * self.fb.w)
        y = int((ty - self.ty_r[0]) / max(1, self.ty_r[1] - self.ty_r[0]) * self.fb.h)
        if y < 80:
            self.pg = ["t", "a", "l"][min(2, x // (self.fb.w // 3))]
        elif self.pg == "a":
            idx = (y - 180) // 100
            active = [a for a in self.data["alarms"] if not a['a']]
            if 0 <= idx < len(active): active[idx]['a'] = True

if __name__ == "__main__":
    try: App().run()
    except KeyboardInterrupt: pass
