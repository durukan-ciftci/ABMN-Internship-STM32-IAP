# ABMN-Internship-STM32-IAP
This project is an integrated embedded system designed to simplify firmware updates and provide visual feedback. It consists of three main components:  Custom STM32 Bootloader and App (Supports In-Application Programming (IAP)), GUI for Firmware Upload via Bluetooth, TouchGFX Interface

## Project Description
This project is an **integrated embedded system** designed to simplify firmware updates and provide visual feedback. It consists of three main components:

1. **Custom STM32 Bootloader and App**
   - Supports **In-Application Programming (IAP)**
   - Enables updating firmware without needing external programmers

2. **GUI for Firmware Upload via Bluetooth**
   - Facilitates the firmware upload process
   - Provides a user-friendly interface to select and send firmware binaries

3. **TouchGFX Interface**
   - Visualizes the update process on the microcontroller display
   - Gives real-time feedback about progress and status

The combination of these components demonstrates a complete workflow for embedded development, from firmware management to visualization.

---

## Features
- STM32 bootloader and app with IAP
- Firmware upload via Bluetooth through a GUI
- Real-time visualization using TouchGFX
- Fully integrated system combining hardware, GUI, and graphical interface

---

## Project Structure
bootloader/ # STM32 bootloader code
application/ # Main application code
gui/ # GUI source code
touchgfx/ # TouchGFX project files
binaries/ # Firmware binaries
README.md # Project documentation

---

## Setup & Usage
1. Flash the **bootloader** and **app** onto the STM32H7A3 Nucleo microcontroller.
2. Run the **GUI application** on your computer.
3. Select the firmware binary and upload it via **Bluetooth**.
4. Monitor the **TouchGFX interface** in STM32F429I Discovery for real-time status and progress.

---

## Technologies
- STM32 Microcontrollers
- TouchGFX, FreeRTOS
- C/C++ (embedded programming)
- GUI (Python)
- Bluetooth and SPI communication
