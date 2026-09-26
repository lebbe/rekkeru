# Rekkeru

Rekkeru is a low-power departure and information display for the Waveshare ESP32-S3-RLCD-4.2. The device normally shows the time, indoor temperature and humidity, and battery level. Pressing its KEY button fetches transit departures, weather, and calendar events.

The project has two parts:

- `firmware/`: Arduino firmware built with PlatformIO. It drives the display and sensors, sleeps between updates, and requests data over Wi-Fi.
- `server/`: A Node.js and Express API that gathers data from Entur, MET Norway, and an optional iCalendar feed. The firmware uses `GET /api/v1/screen`.

## Run the server

Install Node.js LTS, then in `server/` install dependencies and copy `.env.sample` to `.env`. Set a 64-character hexadecimal `API_KEY`, an identifying `UA_STRING`, and optionally `SECRET_ICAL_ADDRESS`. Use the same API key in the firmware.

```sh
npm install
npm start
```

The API listens on port 3000 by default.

## Build and upload the firmware

Install the PlatformIO CLI. Copy `firmware/include/secrets.example.h` to `firmware/include/secrets.h` and set the Wi-Fi credentials, matching `API_KEY`, and `SERVER_URL` to the server's address on your local network.

```sh
cd firmware
pio run
pio run -t upload
```
