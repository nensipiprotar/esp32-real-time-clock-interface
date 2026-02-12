# esp32-real-time-clock-interface

# Project Overview

This project presents a real-time embedded digital clock system built using an ESP32 microcontroller, DS1307 Real-Time Clock (RTC) module, and a TM1637 4-digit 7-segment display.

The system retrieves accurate time data via I2C communication and dynamically displays time, date, and day information using a structured state-machine architecture.

The design emphasizes modular programming, reliable RTC data handling, and non-blocking timing mechanisms suitable for real-world embedded applications.

# System Description

The clock continuously reads real-time data from the DS1307 RTC and cycles between three display modes:

- Time Display (HH:MM format)

- Date Display (DD:MM format)

- Day Indicator (d1–d7)

Mode switching occurs automatically at fixed intervals using a millis()-based non-blocking timing approach, ensuring smooth system operation without delays.

To guarantee stable time reads, the implementation uses an atomic-safe double-read technique to avoid inconsistencies during second transitions.
