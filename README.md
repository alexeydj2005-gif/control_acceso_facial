# Biometric Access Control System Using Facial Recognition

**Advanced Programming Project — C++17**

A desktop prototype developed in C++ that uses facial recognition to identify registered users and record access attempts. The application captures video from a webcam, compares detected faces with a local gallery, displays an access decision, and attempts to save the event in MySQL.

> **Academic prototype:** This software does not operate a physical door or electronic lock and is not ready for real-world security deployment.

## Project objective

The main goal is to apply Advanced Programming concepts in C++ to a practical access-control problem. The system detects faces, recognizes registered people, and keeps a history of allowed and denied access attempts.

## Technologies

- **C++17:** Main application, classes, data structures, and access-control logic.
- **OpenCV 4:** Camera capture, image processing, and face model execution.
- **YuNet (ONNX):** Face detection.
- **SFace (ONNX):** Face alignment, feature extraction, and comparison.
- **MySQL 8 and MySQL Connector/C++:** Storage of registered names and access attempts.
- **Docker Compose:** Local MySQL database environment.
- **CMake:** C++ build configuration.

## Current implementation

- Capture live video using `cv::VideoCapture`.
- Detect faces with YuNet and extract 128-dimensional face features with SFace.
- Compare facial features with a local gallery using cosine similarity.
- Allow a match when its similarity score is at least **0.363** (a configuration value, not a measured accuracy guarantee).
- Register people by capturing **10 facial samples** when exactly one face is visible.
- Save the local face gallery in `galeria.yml` using OpenCV `FileStorage`.
- Remove people from the local gallery.
- Show a **green** rectangle for recognized people and a **red** rectangle for unknown people.
- Record attempts in MySQL (`personas` and `accesos` tables), with a **five-second interval per identity** to avoid repeated entries.
- Continue camera processing during a database interruption and attempt reconnection after **ten seconds**. Access events may not be saved while MySQL is unavailable.

### Advanced C++ concepts used

The project includes a `BaseDatos` class to separate database operations from face-recognition logic. It uses `std::unique_ptr` for resource management, `std::vector` for face samples, `std::unordered_map` for identity-related tracking, `std::chrono` for time intervals, and lambda expressions for filtering registered people.

## Repository structure

```text
control_acceso_facial/
├── src/
│   └── main.cpp                  # Main C++ source code
├── models/
│   ├── face_detection_yunet_2023mar.onnx
│   └── face_recognition_sface_2021dec.onnx
├── CMakeLists.txt                # CMake configuration
├── docker-compose.yml           # MySQL container configuration
├── esquema.sql                  # Database schema
├── .gitignore
└── README.md
```

The `build/` directory is generated locally during compilation. `galeria.yml` contains registered facial data and is deliberately excluded from Git.

## Requirements

This version targets **Linux** (Ubuntu or a compatible distribution, such as Linux Mint). You need:

- A C++17-compatible compiler (for example, `g++`).
- CMake 3.16 or newer.
- OpenCV 4 with the required `objdetect` and `face` modules.
- MySQL Connector/C++ development library (`libmysqlcppconn-dev`).
- Docker with Docker Compose v2.
- A working webcam and the ONNX model files included in `models/`.

## Build and run (Linux)

**1. Clone the repository** (you must have access if it is private):

```bash
git clone https://github.com/alexeydj2005-gif/control_acceso_facial.git
cd control_acceso_facial
```

**2. Start the MySQL container:**

```bash
docker compose up -d
```

Wait until MySQL has initialized before continuing. The database schema is loaded through the Docker Compose configuration when the database volume is initialized for the first time.

**3. Generate the build files and compile:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**4. Run the application:**

```bash
cd build
./control_acceso
```

Running from `build/` is important for relative paths used by the application. Your local MySQL connection settings must match the values used by the application and Docker Compose.

## Keyboard controls

| Key | Action |
| --- | --- |
| `r` | Register a new person (one face must be visible) |
| `d` | Remove a person from the local gallery |
| `Esc` | Exit the application |

## Limitations and future improvements

This is a learning project, not a production-ready authentication system. Recognition can be affected by lighting and camera position. The current version does not include liveness or anti-spoofing detection, physical door control, or formally measured recognition accuracy. Future work includes security testing, safer credential management, and improved protection of biometric information.

**Security note:** Before making this repository public, remove hard-coded database credentials from the source and Compose configuration, rotate exposed credentials where appropriate, and remove sensitive values from Git history. Do not commit `galeria.yml` or other biometric data.

## Project team

- Luciana Betzabe Anchatuna Morales
- Kevin Ariel Andrango Rodriguez
- Jordan Alexey Luna Almeida

**Course:** Advanced Programming  
**Project:** Biometric Access Control System Using Facial Recognition  
**Date:** October 2026
