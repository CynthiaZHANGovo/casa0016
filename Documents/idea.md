# **"Focused Study Feedback Station"**

A small desk device for combined monitoring and adjustment of **time + environment** while you are studying at your desk (light & temperature adjustment, distance reminders, and smart break prompts).

---

### 1. Ultrasonic Distance Sensor

- Detects whether you are sitting at the desk, to record **effective study time**.

- Measures how far you are from the screen, to trigger **“too close” distance reminders**.

### 2. Light Sensor

- Measures ambient/desk brightness (you should at least have the light on when studying).

- When the environment is too dark, it can automatically turn on a lamp or remind you to switch it on.

### 3. (Optional) Noise + Temperature / Humidity

- **Noise**: checks whether the study environment is too noisy.

- **Temperature / Humidity**: checks if the room is too hot, cold, or stuffy.

- These act as additional indicators of overall environment quality.

---

### Logic

- The system only starts recording when it detects that **you are present** at the desk.

- It records your **daily effective study time** and, during those periods, logs the corresponding environment data.

- Based on brightness, temperature, noise level, and distance while you are seated, it produces an **environment quality feedback** for your study sessions.

---

### Real-time Display

- `Today’s focused time: X minutes`

- `Environment: brightness XX, temperature XX, noise XX (optional)`

- `Distance: XX cm (too close / OK)`

---

### Feedback Channels

- **OLED screen**: shows focused time, environment readings, and textual warnings or tips.

- **RGB LED**:

  - Green: you are present and the environment is generally OK.

  - Yellow: minor issues (slightly dark, slightly noisy, etc.).

  - Red: clear problems (too dark, too noisy, or too close to the screen).

---

### “Time to Take a Break” Reminder (Not Just Time-Based)

- The system does **not** trigger breaks based only on “you have sat for N minutes”.

- Instead, it combines:

  1.  You have been in a continuous focused session for a relatively long period (e.g. > 30–45 minutes), **and**

  2.  Your environment or posture starts to degrade:

      - brightness getting too low,

      - temperature drifting to an uncomfortable range,

      - noise level increasing, or

      - you are sitting increasingly close to the screen.

- Only when **“long enough focus time” + “worsening environment/behavior”** happen together, the system triggers a **“time to take a break”** reminder (via OLED message + RGB LED change).

This way, the “time’s up” signal is not just a simple timer, but a **combined decision based on time, environment data, and your sitting behavior**.
