# vMix Tally Light - Nixtown

A lightweight ESP32-based tally light system for use with vMix. Designed for live production setups that need visual indicators for **Preview** and **Program** states.

---

## 📦 Features

* Connects to vMix over TCP (port 8099)
* Displays tally light color for **Preview** and **Program** states
* Web-based configuration portal
* Color pickers for user-defined preview/program LED colors
* Auto AP mode if Wi-Fi is not configured
* **Double reset** to force AP mode again (unplug during rainbow)
* mDNS support (access via `http://device-name.local`)

---

## 🚦 Boot-Up Color Reference

| Color Pattern      | Meaning                            |
| ------------------ | ---------------------------------- |
| 🌈 Rainbow         | Initializing (may occur twice)     |
| ✅ 3 Green Blinks   | Connected to Wi-Fi                 |
| 🟣 3 Purple Blinks | Connected to vMix                  |
| 🔵 Blue Pulse      | AP Mode active (connect to portal) |
| 🟡 3 Yellow Blinks | Reset fallback (settings cleared)  |

---

## 🌐 How It Works

When the device powers on:

1. It tries to connect to saved Wi-Fi.
2. If it fails, it enters **AP Mode** and shows a **blue pulsing LED**.
3. You can connect to it via Wi-Fi and open the **setup portal**.
4. After connecting to Wi-Fi, visit `http://tallylight-0.local` to finish setup.
5. **Important:** If you're deploying multiple devices, change the device name after connecting to avoid mDNS conflicts.

If you forget the device name, perform a **Quick Reset**:

* Unplug the device **while rainbow animation is showing**.
* On next boot, it will **clear settings and return to AP mode**.

---

## 🌈 Web Portal

Access the configuration page at `http://<device-name>.local` or the IP shown in the serial log. From here, you can:

* Change Device Name
* Set vMix IP address
* Define the Input Number
* Pick Preview and Active Colors
* Reset settings to default

---

## 🔌 Finding Your vMix IP

Inside vMix:

* Go to **Settings → Web Controller**
* Your computer’s IP address will be listed there (e.g. `192.168.1.25`)
* Enter this into the "vMix IP" field on the tally light web page

---

## 🔁 Reset Methods

| Method                 | Effect                             |
| ---------------------- | ---------------------------------- |
| Reset button           | Restarts device normally           |
| Unplug during rainbow  | Clears settings and enters AP mode |
| Reset button during AP | Stays in AP mode                   |

---

Here’s your updated section with a clear note about the board compatibility:

---

## 🔧 Arduino Setup

### ✅ Required Libraries

Install these via **Arduino Library Manager**:

* `WiFiManager` by tzapu
* `FastLED` by Daniel Garcia
* `ESPmDNS`
* `Preferences`
* `LittleFS`

Also make sure you’ve installed the **ESP32 board support** from **Espressif** via **Board Manager**.

---

### ⚠️ Board Selection

Although the hardware is sold as **Teyleten Robot ESP32-C3**,
you **must select** the board as:

```
LOLIN C3 Mini
```

in the Arduino IDE.
This code only works properly when compiled for the **LOLIN C3 Mini** variant due to pin mappings and USB behavior.


### 📂 Filesystem Upload (LittleFS)

To upload the HTML/CSS config page to your ESP32:

1. Create a folder named `/data` in your sketch directory.
2. Put your `index.html` file there.
3. Use the **ESP32 LittleFS Upload Tool** below.

### 🧩 Installing LittleFS Plugin in Arduino IDE 2.x

Arduino IDE 2.x does **not** use `.jar` plugins like the older IDE. Instead, it uses `.vsix` files.

#### Option 1: Use the included plugin

This repo includes `arduino-littlefs-upload-1.5.4.vsix`. To install it:

1. Move the file to this location:

   ```
   ~/.arduinoIDE/deployedPlugins
   ```

   *(Create the folder if it doesn’t exist.)*
2. Restart Arduino IDE.

#### Option 2: Download the latest version

Get the newest release from:
👉 [https://github.com/earlephilhower/arduino-littlefs-upload/releases](https://github.com/earlephilhower/arduino-littlefs-upload/releases)
\~/.arduinoIDE/deployedPlugins

```
*(On macOS. Create the folder if it doesn’t exist.)*

3. Restart the Arduino IDE.

4. Press `Cmd+Shift+P` (Mac) or `Ctrl+Shift+P` (Windows/Linux)
5. Run:
```

ESP32: LittleFS Data Upload

```

This uploads your `/data` files to ESP32 flash memory.

---

Absolutely! Here's a new section you can add to your README or release notes to guide users on what to buy:

---

## 🛒 Shopping List (Amazon)

To build your own Tally Light system, you'll need the following components — all available on Amazon:

### 🔌 Core Components

* [**Teyleten Robot ESP32-C3 Development Board (3-pack)**](https://www.amazon.com/hz/wishlist/ls/Y0TJ246P24J9?ref_=wl_share)
  Compact ESP32-C3 Supermini boards with WiFi & Bluetooth — perfect for small enclosures and low power use.

* [**BTF-LIGHTING WS2812B RGB LED Strip (144 LEDs/m, 1 meter)**](https://www.amazon.com/hz/wishlist/ls/Y0TJ246P24J9?ref_=wl_share)
  High-density individually addressable LEDs with vivid color — ideal for clean animation and clear status indicators.

⚠️ **Note:** This LED strip does not come with a power adapter or controller — but power is provided via USB in this project.

---

Let me know if you’d like help turning that into a collapsible section or want to include wiring instructions too.


## 📃 License

MIT License — do whatever you want, just don’t blame me 😄

---

Created with ❤️ by [@nix_the_world](https://github.com/yourusername) for live streaming setups everywhere.

```
