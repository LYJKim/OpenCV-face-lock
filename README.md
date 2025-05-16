# 📌 OpenCV Face Lock

## 🔒 Project Overview

**OpenCV Face Lock** is a facial recognition-based door lock system that enhances security and user convenience for dormitory access using Raspberry Pi.

The system provides the following features:

- Facial recognition-based authentication (OpenCV + LBPH)
- Two-step authentication with password
- Intruder detection and admin alert
- Distributed control using 4 Raspberry Pi devices
- Implemented using C, Python, MariaDB, and socket communication

---

## 📁 Project Structure

```plaintext
OpenCV-face-lock/
├── report/                  # Project report
│   └── report.pdf
├── codes/                   # Code directory
│   ├── Pi1/                 # User input and image capture
│   │   └── pi1.c
│   ├── Pi2/                 # Main server and sensor control
│   │   ├── db.c
│   │   ├── sensor.c
│   │   └── server.zip       # Java server code (zipped)
│   ├── Pi3/                 # Keypad input and motor control
│   │   └── pi3.c
│   └── Pi4/                 # Face recognition and motion detection
│       ├── pi4_img_rec.c
│       ├── pi4_pir.c
│       ├── raspi_dataset+learning.py   # Training script
│       └── raspi_predict.py            # Prediction script
├── .gitignore
└── README.md
```

---

## 🚀 How to Run

### 1. Set up Raspberry Pi environment
- Install Ubuntu or Raspberry Pi OS on each Pi
- Install required dependencies (OpenCV, MariaDB, etc.)

### 2. Deploy code to each Pi
- `codes/Pi1/pi1.c` → For user input and photo capture Pi
- `codes/Pi2/db.c`, `sensor.c` → For the main server Pi
- `codes/Pi3/pi3.c` → For the door lock control Pi
- `codes/Pi4/` (C & Python files) → For face recognition and motion sensing Pi

### 3. Train the model
- Run `raspi_dataset+learning.py` to train the model with registered face images

### 4. Execute system
- When a person is detected via PIR sensor → camera captures an image → face recognition
- If the face is recognized, the student ID is sent → password is checked → door unlocks if matched

---


