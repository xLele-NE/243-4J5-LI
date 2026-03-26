import threading
import time
from queue import Queue

import curses
from evdev import InputDevice, ecodes, list_devices
import paho.mqtt.client as mqtt
import ssl

# Configuration MQTT
from mqtt_config import MQTT_CONFIG


# ---------- GESTION DU TOUCH ----------

class TouchReader(threading.Thread):
    def __init__(self, event_queue: Queue):
        super().__init__(daemon=True)
        self.event_queue = event_queue
        self.device = self._find_touch_device()
        if not self.device:
            raise RuntimeError("Aucun périphérique touchscreen trouvé.")

        # On récupère les infos d'axes pour calibrer
        abs_x = self.device.absinfo(ecodes.ABS_MT_POSITION_X)
        abs_y = self.device.absinfo(ecodes.ABS_MT_POSITION_Y)

        self.min_x, self.max_x = abs_x.min, abs_x.max
        self.min_y, self.max_y = abs_y.min, abs_y.max

        self.current_x = (self.min_x + self.max_x) // 2
        self.current_y = (self.min_y + self.max_y) // 2

    def _find_touch_device(self):
        """
        Essaie de trouver un device dont le nom contient 'touch' ou 'ft5406'
        (fréquent sur les écrans Raspberry Pi).
        """
        for path in list_devices():
            dev = InputDevice(path)
            name = dev.name.lower()
            if "touch" in name or "ft5406" in name:
                print(f"[TouchReader] Using device: {dev.name} ({path})")
                return dev
        return None

    def run(self):
        for event in self.device.read_loop():
            if event.type == ecodes.EV_ABS:
                if event.code == ecodes.ABS_MT_POSITION_X:
                    self.current_x = event.value
                elif event.code == ecodes.ABS_MT_POSITION_Y:
                    self.current_y = event.value

            elif event.type == ecodes.EV_KEY and event.code == ecodes.BTN_TOUCH:
                # 1 = touch down, 0 = touch up
                if event.value == 1:
                    # On push un "tap" dans la queue avec les coordonnées brutes
                    self.event_queue.put(("tap", self.current_x, self.current_y))


# ---------- UI CURSES ----------

class LEDControlUI:
    def __init__(self, stdscr, touch_reader: TouchReader, event_queue: Queue, mqtt_config: dict):
        self.stdscr = stdscr
        self.touch_reader = touch_reader
        self.event_queue = event_queue
        self.running = True
        self.status_message = "Prêt - Contrôle des LEDs via MQTT"

        # Configuration MQTT
        self.mqtt_config = mqtt_config
        self.mqtt_client = None
        self.mqtt_connected = False

        # Topics MQTT pour les LEDs du LilyGo
        device_id = mqtt_config.get("device_id", "esp32-XXXX")
        self.led1_topic = f"{device_id}/led/1/set"
        self.led2_topic = f"{device_id}/led/2/set"
        
        # Topics d'état pour mise à jour UI depuis boutons physiques
        self.led1_state_topic = f"{device_id}/led/1/state"
        self.led2_state_topic = f"{device_id}/led/2/state"
        self.led3_state_topic = f"{device_id}/led/3/state"
        self.pot_state_topic = f"{device_id}/potentiometer/state"
        self.accel_state_topic = f"{device_id}/accelerometer/orientation"
        self.simon_status_topic = f"{device_id}/simon/status"
        self.simon_score_topic = f"{device_id}/simon/score"
        self.simon_start_topic = f"{device_id}/simon/start"
        self.simon_level_topic = f"{device_id}/simon/level"

        self._init_mqtt()

        self.buttons = []  # rempli à chaque redraw en fonction de la taille écran

        # États des LEDs (pour les toggle switches)
        self.led1_state = False  # False = OFF, True = ON
        self.led2_state = False
        self.led3_state = False
        self.pot_value = 0 # 0 à 100
        self.orientation = "vertical_left"  # Orientation verticale par défaut
        self.simon_status = "IDLE"
        self.simon_score = 0
        self.simon_level = 1
        self.simon_record = 0  # Record de niveau atteint

        # Buffer pour les messages MQTT reçus (max 10 lignes)
        self.mqtt_feedback = []
        self.max_feedback_lines = 10

    def _init_mqtt(self):
        """
        Initialise la connexion MQTT via WebSocket Secure (WSS)
        """
        try:
            # Créer le client MQTT avec transport WebSocket
            client_id = f"python-control-{int(time.time())}"
            self.mqtt_client = mqtt.Client(
                client_id=client_id,
                transport="websockets"
            )

            # Configuration SSL pour WSS (Insecure pour la démo)
            self.mqtt_client.tls_set(
                ca_certs=None,
                certfile=None,
                keyfile=None,
                cert_reqs=ssl.CERT_NONE,
                tls_version=ssl.PROTOCOL_TLS,
                ciphers=None
            )
            self.mqtt_client.tls_insecure_set(True)

            # Authentification
            username = self.mqtt_config.get("username", "esp_user")
            password = self.mqtt_config.get("password", "")
            self.mqtt_client.username_pw_set(username, password)

            # Callbacks
            self.mqtt_client.on_connect = self._on_mqtt_connect
            self.mqtt_client.on_disconnect = self._on_mqtt_disconnect
            self.mqtt_client.on_message = self._on_mqtt_message

            # Connexion
            broker = self.mqtt_config.get("broker", "mqtt.edxo.ca")
            port = self.mqtt_config.get("port", 443)

            self.status_message = f"Connexion à {broker}:{port}..."
            self.mqtt_client.connect(broker, port, 60)

            # Démarrer la boucle réseau dans un thread
            self.mqtt_client.loop_start()

        except Exception as e:
            self.status_message = f"Erreur MQTT: {str(e)}"
            self.mqtt_client = None

    def _on_mqtt_connect(self, client, userdata, flags, rc):
        """Callback appelé lors de la connexion MQTT"""
        if rc == 0:
            self.mqtt_connected = True
            self.status_message = "MQTT connecté!"
            self._add_feedback("✓ Connecté au broker MQTT")

            # S'abonner aux topics de statut des boutons et des LEDs (pour mise à jour via bouton physique)
            client.subscribe(self.led1_state_topic)
            client.subscribe(self.led2_state_topic)
            client.subscribe(self.led3_state_topic)
            client.subscribe(self.pot_state_topic)
            client.subscribe(self.accel_state_topic)
            client.subscribe(self.simon_status_topic)
            client.subscribe(self.simon_score_topic)
            client.subscribe(self.simon_level_topic)

        else:
            self.mqtt_connected = False
            error_messages = {
                1: "Protocole incorrect",
                2: "Client ID rejeté",
                3: "Serveur indisponible",
                4: "Username/Password incorrect",
                5: "Non autorisé"
            }
            msg = error_messages.get(rc, f"Erreur inconnue ({rc})")
            self.status_message = f"Échec connexion MQTT: {msg}"
            self._add_feedback(f"✗ Erreur: {msg}")

    def _on_mqtt_disconnect(self, client, userdata, rc):
        """Callback appelé lors de la déconnexion MQTT"""
        self.mqtt_connected = False
        if rc != 0:
            self.status_message = f"Déconnexion MQTT inattendue (code {rc})"
            self._add_feedback("⚠ Connexion perdue, reconnexion...")

    def _on_mqtt_message(self, client, userdata, msg):
        """Callback appelé lors de la réception d'un message MQTT"""
        topic = msg.topic
        payload = msg.payload.decode('utf-8', errors='ignore')
        self._add_feedback(f"← {topic}: {payload}")

        # Si l'état change physiquement, mettre à jour l'UI
        if topic == self.led1_state_topic:
            if payload == "ON":
                self.led1_state = True
            elif payload == "OFF":
                self.led1_state = False
        elif topic == self.led2_state_topic:
            if payload == "ON":
                self.led2_state = True
            elif payload == "OFF":
                self.led2_state = False
        elif topic == self.led3_state_topic:
            if payload == "ON":
                self.led3_state = True
            elif payload == "OFF":
                self.led3_state = False
        elif topic == self.pot_state_topic:
            try:
                # payload est entre 0 et 100
                pot_val = int(payload)
                self.pot_value = max(0, min(100, pot_val))
                
                # Ajuster la luminosité de l'écran (0 à 31 pour 11-0045)
                # Map 0-100 to 0-31
                brightness = int((self.pot_value / 100.0) * 31)
                brightness = max(1, min(31, brightness)) # Éviter l'écran totalement noir
                try:
                    with open("/sys/class/backlight/11-0045/brightness", "w") as f:
                        f.write(str(brightness))
                except Exception as e:
                    self._add_feedback(f"Erreur LCD: {str(e)}")
                    
            except ValueError:
                pass
        elif topic == self.accel_state_topic:
            new_orientation = payload.strip()
            if new_orientation != self.orientation:
                self.orientation = new_orientation
                self._add_feedback(f"Orientation: {self.orientation}")
                
                # Rotation physique de l'écran
                rotate_val = "0"
                if self.orientation == "vertical_left":
                    rotate_val = "1"
                elif self.orientation == "horizontal_down":
                    rotate_val = "2"
                elif self.orientation == "vertical_right":
                    rotate_val = "3"
                elif self.orientation in ("horizontal_up", "flat"):
                    rotate_val = "0"
                    
                try:
                    with open("/sys/class/graphics/fbcon/rotate", "w") as f:
                        f.write(rotate_val)
                except:
                    pass
                
                # Redessiner avec les nouvelles dimensions
                import time
                time.sleep(0.3)
                try:
                    curses.resizeterm(0, 0)
                except:
                    pass
                self.stdscr.erase()
                self._draw()
                self.stdscr.refresh()
        elif topic == self.simon_status_topic:
            self.simon_status = payload
            self.status_message = f"SIMON: {self.simon_status}"
            # Forcer un redessin pour afficher BRAVO en gros
            self.stdscr.erase()
            self._draw()
            self.stdscr.refresh()
        elif topic == self.simon_score_topic:
            try:
                self.simon_score = int(payload)
            except:
                pass
        elif topic == self.simon_level_topic:
            try:
                self.simon_level = int(payload)
                if self.simon_level > self.simon_record:
                    self.simon_record = self.simon_level
            except:
                pass

    def _add_feedback(self, message):
        """Ajoute un message au buffer de feedback"""
        self.mqtt_feedback.append(message)
        if len(self.mqtt_feedback) > self.max_feedback_lines:
            self.mqtt_feedback.pop(0)

    def _publish_mqtt(self, topic, message):
        """
        Publie un message MQTT
        """
        if self.mqtt_client and self.mqtt_connected:
            try:
                result = self.mqtt_client.publish(topic, message, qos=0)
                if result.rc == mqtt.MQTT_ERR_SUCCESS:
                    self.status_message = f"Envoyé: {topic} = {message}"
                    self._add_feedback(f"→ {topic}: {message}")
                else:
                    self.status_message = f"Erreur publication: {result.rc}"
            except Exception as e:
                self.status_message = f"Erreur: {str(e)}"
        else:
            self.status_message = "MQTT non connecté"

    def _draw_big_text(self, text, start_row, center_col, attr):
        """
        Dessine du texte en ASCII art 3x5 (chaque lettre fait 3 colonnes x 5 lignes)
        """
        # Police ASCII art simplifiée pour ON/OFF
        font = {
            'O': [
                "███",
                "█ █",
                "█ █",
                "█ █",
                "███"
            ],
            'N': [
                "███",
                "█ █",
                "█ █",
                "█ █",
                "█ █"
            ],
            'F': [
                "███",
                "█  ",
                "██ ",
                "█  ",
                "█  "
            ],
            ' ': [
                "   ",
                "   ",
                "   ",
                "   ",
                "   "
            ]
        }

        # Calculer la largeur totale
        total_width = len(text) * 4  # 3 pour la lettre + 1 d'espacement
        start_col = center_col - total_width // 2

        # Dessiner chaque ligne
        h, w = self.stdscr.getmaxyx()
        for line_idx in range(5):
            row = start_row + line_idx
            if 0 <= row < h:
                col = start_col
                line_text = ""
                for char in text.upper():
                    if char in font:
                        line_text += font[char][line_idx] + " "
                    else:
                        line_text += "    "

                if col >= 0 and col + len(line_text) < w:
                    self.stdscr.attron(attr)
                    self.stdscr.addstr(row, col, line_text)
                    self.stdscr.attroff(attr)

    def _init_colors(self):
        curses.start_color()
        # On définit MAGENTA comme fond pour donner un effet "Rose"
        curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_YELLOW)   # bouton QUIT
        curses.init_pair(2, curses.COLOR_BLACK, curses.COLOR_GREEN)    # bouton actif
        curses.init_pair(3, curses.COLOR_WHITE, curses.COLOR_MAGENTA)  # texte status
        curses.init_pair(4, curses.COLOR_WHITE, curses.COLOR_RED)      # LED ROUGE ON (Blanc sur Rouge)
        curses.init_pair(5, curses.COLOR_WHITE, curses.COLOR_GREEN)    # LED VERTE ON (Blanc sur Vert)
        curses.init_pair(6, curses.COLOR_BLACK, curses.COLOR_RED)      # LED ROUGE OFF (Noir sur Rouge)
        curses.init_pair(7, curses.COLOR_BLACK, curses.COLOR_GREEN)    # LED VERTE OFF (Noir sur Vert)
        curses.init_pair(8, curses.COLOR_WHITE, curses.COLOR_MAGENTA)  # Bordures
        curses.init_pair(9, curses.COLOR_BLACK, curses.COLOR_CYAN)     # Titre
        curses.init_pair(10, curses.COLOR_WHITE, curses.COLOR_MAGENTA) # Paire pour le fond global (ROSE)
        curses.init_pair(11, curses.COLOR_WHITE, curses.COLOR_BLUE)    # LED BLEUE ON
        curses.init_pair(12, curses.COLOR_BLACK, curses.COLOR_BLUE)    # LED BLEUE OFF

    def _build_buttons(self, h, w):
        """
        5 boutons : START + 3 LEDs (Rouge, Vert, Bleu) + QUITTER
        Layout vertical (boutons empilés) car l'orientation par défaut est verticale
        """
        self.buttons = []
        is_vertical = "vertical" in self.orientation
        
        safe_h = h if h > 5 else 24
        safe_w = w if w > 5 else 80
        device_id = self.mqtt_config.get("device_id", "esp32-XXXX")

        buttons_config = [
            {"name": "LED1", "label": "LED ROUGE (P25)", "state_attr": "led1_state", "topic": self.led1_topic, "color_on": 4, "color_off": 6},
            {"name": "LED2", "label": "LED VERTE (P33)", "state_attr": "led2_state", "topic": self.led2_topic, "color_on": 5, "color_off": 7},
            {"name": "LED3", "label": "LED BLEUE (P12)", "state_attr": "led3_state", "topic": f"{device_id}/led/3/set", "color_on": 11, "color_off": 12},
        ]

        if is_vertical:
            # Layout vertical : boutons empilés, larges
            btn_width = min(50, safe_w - 6)
            btn_height = 4
            current_row = 8
            for btn_cfg in buttons_config:
                self.buttons.append({
                    "name": btn_cfg["name"], "label": btn_cfg["label"],
                    "state_attr": btn_cfg["state_attr"], "topic": btn_cfg["topic"],
                    "row": current_row, "col": (safe_w - btn_width) // 2,
                    "height": btn_height, "width": btn_width,
                    "active": False, "color_on": btn_cfg["color_on"], "color_off": btn_cfg["color_off"],
                })
                current_row += btn_height + 1
            # START
            self.buttons.append({
                "name": "START", "label": "  ▶ LANCER SIMON  ",
                "state_attr": None, "topic": self.simon_start_topic,
                "row": current_row, "col": (safe_w - btn_width) // 2,
                "height": 4, "width": btn_width,
                "active": False, "color_on": 2, "color_off": 2,
            })
        else:
            # Layout horizontal : boutons côte à côte
            self.buttons.append({
                "name": "START", "label": "  ▶ LANCER SIMON  ",
                "state_attr": None, "topic": self.simon_start_topic,
                "row": 9, "col": (safe_w - min(45, safe_w - 6)) // 2,
                "height": 5, "width": min(45, safe_w - 6),
                "active": False, "color_on": 2, "color_off": 2,
            })
            spacing = 2
            led_btn_width = max(18, (safe_w - 2 * spacing - 4) // 3)
            led_btn_height = 6
            led_row = 15
            total_width = 3 * led_btn_width + 2 * spacing
            start_col = (safe_w - total_width) // 2
            for i, btn_cfg in enumerate(buttons_config):
                col = start_col + i * (led_btn_width + spacing)
                self.buttons.append({
                    "name": btn_cfg["name"], "label": btn_cfg["label"],
                    "state_attr": btn_cfg["state_attr"], "topic": btn_cfg["topic"],
                    "row": led_row, "col": col, "height": led_btn_height, "width": led_btn_width,
                    "active": False, "color_on": btn_cfg["color_on"], "color_off": btn_cfg["color_off"],
                })

        # Bouton QUITTER (toujours en bas)
        quit_row = safe_h - 3
        if quit_row < 1:
            quit_row = safe_h - 1
        self.buttons.append({
            "name": "QUIT", "label": " ✕ QUITTER SIMON ",
            "state_attr": None, "topic": None,
            "row": quit_row, "col": (safe_w - 28) // 2,
            "height": 2, "width": 28,
            "active": False, "color_on": 1, "color_off": 1,
        })

    def _draw(self):
        # Appliquer le fond ROSE (Magenta)
        self.stdscr.bkgd(' ', curses.color_pair(10))
        self.stdscr.erase()
        h, w = self.stdscr.getmaxyx()

        # --- Ligne 0 : Titre ---
        title = "═══ JEU SIMON IOT ═══"
        self.stdscr.attron(curses.color_pair(9) | curses.A_BOLD)
        if len(title) < w:
            self.stdscr.addstr(0, max(0, (w - len(title)) // 2), title)
        self.stdscr.attroff(curses.color_pair(9) | curses.A_BOLD)

        # --- Ligne 1 : Connexion ---
        if self.mqtt_connected:
            conn = " CONNECTÉ "
            conn_color = curses.color_pair(5) | curses.A_BOLD
        else:
            conn = " DÉCONNECTÉ "
            conn_color = curses.color_pair(4) | curses.A_BOLD
        self.stdscr.attron(conn_color)
        if len(conn) < w:
            self.stdscr.addstr(1, max(0, (w - len(conn)) // 2), conn)
        self.stdscr.attroff(conn_color)

        # --- Zone Simon (lignes 2 à 7) ---
        if self.simon_status == "BRAVO":
            # BRAVO en ASCII art
            bravo_lines = [
                "██████  ██████   █████  ██    ██  ██████ ",
                "██   ██ ██      ██   ██ ██    ██ ██      ",
                "██████  █████   ███████ ██    ██ ██   ███",
                "██   ██ ██      ██   ██  ██  ██  ██    ██",
                "██████  ██████  ██    ██   ████    ██████ ",
            ]
            for i, line in enumerate(bravo_lines):
                row = 3 + i
                if row < h - 5:
                    col = max(0, (w - len(line)) // 2)
                    if col + len(line) < w:
                        self.stdscr.attron(curses.color_pair(5) | curses.A_BOLD | curses.A_REVERSE)
                        self.stdscr.addstr(row, col, line)
                        self.stdscr.attroff(curses.color_pair(5) | curses.A_BOLD | curses.A_REVERSE)
        elif self.simon_status == "PERDU":
            perdu_text = "PERDU !"
            self.stdscr.attron(curses.color_pair(4) | curses.A_BOLD | curses.A_REVERSE)
            if len(perdu_text) < w:
                self.stdscr.addstr(4, max(0, (w - len(perdu_text)) // 2), perdu_text)
            self.stdscr.attroff(curses.color_pair(4) | curses.A_BOLD | curses.A_REVERSE)
        else:
            # Section Simon
            section_title = "── JEU SIMON ──"
            self.stdscr.attron(curses.color_pair(8) | curses.A_BOLD)
            if len(section_title) < w:
                self.stdscr.addstr(2, max(0, (w - len(section_title)) // 2), section_title)
            self.stdscr.attroff(curses.color_pair(8) | curses.A_BOLD)

            # Niveau en texte simple
            lvl_text = f" ★ NIVEAU {self.simon_level} ★ "
            self.stdscr.attron(curses.color_pair(3) | curses.A_BOLD | curses.A_REVERSE)
            if len(lvl_text) < w:
                self.stdscr.addstr(3, max(0, (w - len(lvl_text)) // 2), lvl_text)
            self.stdscr.attroff(curses.color_pair(3) | curses.A_BOLD | curses.A_REVERSE)

            # Record
            record_text = f" RECORD: {self.simon_record} "
            self.stdscr.attron(curses.color_pair(4) | curses.A_BOLD)
            if len(record_text) < w:
                self.stdscr.addstr(4, max(0, (w - len(record_text)) // 2), record_text)
            self.stdscr.attroff(curses.color_pair(4) | curses.A_BOLD)

            # Statut du jeu
            status_display = f" {self.simon_status} "
            status_color = curses.color_pair(2) | curses.A_BOLD if "YOUR TURN" in self.simon_status else curses.color_pair(8) | curses.A_BOLD
            self.stdscr.attron(status_color)
            if len(status_display) < w:
                self.stdscr.addstr(5, max(0, (w - len(status_display)) // 2), status_display)
            self.stdscr.attroff(status_color)

        # --- Boutons ---
        self._build_buttons(h, w)

        for btn in self.buttons:
            is_on = False
            if btn["state_attr"]:
                is_on = getattr(self, btn["state_attr"], False)

            color_pair = btn["color_on"] if is_on else btn["color_off"]
            attr = curses.color_pair(color_pair) | curses.A_BOLD
            border_attr = curses.color_pair(8) | curses.A_BOLD

            row_top = btn["row"]
            row_bottom = btn["row"] + btn["height"] - 1
            col_left = btn["col"]
            col_right = btn["col"] + btn["width"] - 1

            if 0 <= row_top < h:
                self.stdscr.attron(border_attr)
                self.stdscr.addstr(row_top, col_left, "╔" + "═" * (btn["width"] - 2) + "╗")
                self.stdscr.attroff(border_attr)

            for r in range(row_top + 1, row_bottom):
                if 0 <= r < h:
                    self.stdscr.attron(border_attr)
                    self.stdscr.addstr(r, col_left, "║")
                    self.stdscr.attroff(border_attr)
                    self.stdscr.attron(attr)
                    self.stdscr.addstr(r, col_left + 1, " " * (btn["width"] - 2))
                    self.stdscr.attroff(attr)
                    self.stdscr.attron(border_attr)
                    self.stdscr.addstr(r, col_right, "║")
                    self.stdscr.attroff(border_attr)

            if 0 <= row_bottom < h:
                self.stdscr.attron(border_attr)
                self.stdscr.addstr(row_bottom, col_left, "╚" + "═" * (btn["width"] - 2) + "╝")
                self.stdscr.attroff(border_attr)

            # Label
            label = btn["label"][:btn["width"] - 4]
            label_col = btn["col"] + max(0, (btn["width"] - len(label)) // 2)
            label_row = btn["row"] + btn["height"] // 2
            if 0 <= label_row < h and label_col + len(label) < w:
                self.stdscr.attron(attr | curses.A_UNDERLINE)
                self.stdscr.addstr(label_row, label_col, label)
                self.stdscr.attroff(attr | curses.A_UNDERLINE)

            # ON/OFF
            if btn["state_attr"] and is_on:
                on_row = label_row + 1
                if 0 <= on_row < h:
                    on_text = "  ON  "
                    on_col = btn["col"] + max(0, (btn["width"] - len(on_text)) // 2)
                    self.stdscr.attron(attr | curses.A_REVERSE)
                    self.stdscr.addstr(on_row, on_col, on_text)
                    self.stdscr.attroff(attr | curses.A_REVERSE)

        # Potentiomètre en bas
        pot_row = h - 1
        if pot_row > 20:
            pot_label = f"Luminosité: {self.pot_value}%"
            bar_width = w - len(pot_label) - 6
            if bar_width > 5:
                filled = int((self.pot_value / 100.0) * bar_width)
                empty = bar_width - filled
                bar_str = "[" + "█" * filled + "░" * empty + "]"
                self.stdscr.attron(curses.color_pair(9) | curses.A_BOLD)
                self.stdscr.addstr(pot_row, 2, pot_label + " " + bar_str)
                self.stdscr.attroff(curses.color_pair(9) | curses.A_BOLD)

        self.stdscr.refresh()

    def _touch_to_rowcol(self, x_raw, y_raw):
        """
        Map coordonnées brutes evdev -> lignes/colonnes du terminal curses.
        """
        h, w = self.stdscr.getmaxyx()

        # protection division par zéro
        dx = max(1, self.touch_reader.max_x - self.touch_reader.min_x)
        dy = max(1, self.touch_reader.max_y - self.touch_reader.min_y)

        x_norm = (x_raw - self.touch_reader.min_x) / dx
        y_norm = (y_raw - self.touch_reader.min_y) / dy

        col = int(x_norm * (w - 1))
        row = int(y_norm * (h - 1))

        # clamp
        row = max(0, min(h - 1, row))
        col = max(0, min(w - 1, col))
        return row, col

    def _handle_touch_tap(self, x_raw, y_raw):
        row, col = self._touch_to_rowcol(x_raw, y_raw)

        # Vérifier sur quel bouton on a tapé
        clicked_btn = None
        for btn in self.buttons:
            if (btn["row"] <= row < btn["row"] + btn["height"] and
                    btn["col"] <= col < btn["col"] + btn["width"]):
                clicked_btn = btn
                break

        if not clicked_btn:
            self.status_message = f"Touché hors bouton: row={row}, col={col}"
            return

        # Traiter le clic selon le bouton
        btn_name = clicked_btn["name"]

        if btn_name == "QUIT":
            self.simon_status = "IDLE"
            self.status_message = "Simon arrêté - mode libre"
            self._publish_mqtt(self.simon_start_topic, "STOP")
            self.stdscr.erase()
            self._draw()
            self.stdscr.refresh()
        elif btn_name == "LED1":
            # Toggle LED1
            self.led1_state = not self.led1_state
            message = "ON" if self.led1_state else "OFF"
            self._publish_mqtt(clicked_btn["topic"], message)
            self.status_message = f"LED ROUGE: {message}"
        elif btn_name == "LED2":
            # Toggle LED2
            self.led2_state = not self.led2_state
            message = "ON" if self.led2_state else "OFF"
            self._publish_mqtt(clicked_btn["topic"], message)
            self.status_message = f"LED VERTE: {message}"
        elif btn_name == "LED3":
            # Toggle LED3
            self.led3_state = not getattr(self, 'led3_state', False)
            message = "ON" if self.led3_state else "OFF"
            self._publish_mqtt(clicked_btn["topic"], message)
            self.status_message = f"LED BLEUE: {message}"
        elif btn_name == "START":
            # Envoyer commande de démarrage Simon
            self._publish_mqtt(clicked_btn["topic"], "START")
            self.status_message = "Démarrage Simon..."

    def run(self):
        self.stdscr.nodelay(True)
        curses.curs_set(0)
        self._init_colors()

        # Rotation initiale : écran en mode vertical (90 degrés)
        try:
            with open("/sys/class/graphics/fbcon/rotate", "w") as f:
                f.write("1")  # 1 = 90 degrés (vertical à gauche)
        except:
            pass

        last_redraw = 0

        while self.running:
            now = time.time()
            if now - last_redraw > 0.05:  # ~20 FPS
                self._draw()
                last_redraw = now

            # Lecture touches clavier
            try:
                ch = self.stdscr.getch()
            except curses.error:
                ch = -1

            if ch == ord('q'):
                self.status_message = "Quit avec 'q'."
                self.running = False

            # Gestion des événements tactiles
            try:
                event = self.event_queue.get_nowait()
            except Exception:
                event = None

            if event:
                kind, x_raw, y_raw = event
                if kind == "tap":
                    self._handle_touch_tap(x_raw, y_raw)

            time.sleep(0.01)

        # Fermer la connexion MQTT à la fin
        if self.mqtt_client:
            self.mqtt_client.loop_stop()
            self.mqtt_client.disconnect()


# ---------- ENTRY POINT ----------

def main(stdscr):
    event_queue = Queue()
    touch_reader = TouchReader(event_queue)
    touch_reader.start()

    ui = LEDControlUI(stdscr, touch_reader, event_queue, MQTT_CONFIG)
    ui.run()


if __name__ == "__main__":
    curses.wrapper(main)
