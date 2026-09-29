# Rekkeru

Rekkeru is a low-power departure and information display for the Waveshare ESP32-S3-RLCD-4.2. The device normally shows the time, indoor temperature and humidity, and battery level. Pressing its KEY button fetches transit departures, weather, and calendar events. Pressing KEY again opens a voice chat with an animated face; press BOOT to switch personality and KEY to go back.

The project has two parts:

- `firmware/`: Arduino firmware built with PlatformIO. It drives the display and sensors, sleeps between updates, and requests data over Wi-Fi.
- `server/`: A Node.js and Express API that gathers data from Entur, MET Norway, and an optional iCalendar feed. The firmware uses `GET /api/v1/screen`.

## Run the server

### Install dependencies

Install Node.js LTS, then in `server/` install dependencies:

```sh
npm ci
```

### Create .env

Copy `.env.sample` to `.env`. Set a 64-character hexadecimal `API_KEY`, an identifying `UA_STRING`, and `SECRET_ICAL_ADDRESS`. Use the same API key in the firmware.

- `API_KEY` Unique secret 64-character hexadecimal.
- `UA_STRING` A string that identifyes you, used as user agent string.
- `SECRET_ICAL_ADDRESS` Point to an URL delivering your calendar in ical format.
- `DEPLOY_HOST` Used for deploying, username and host on deploy server.
- `WEATHER_QUERY` query-part of met.no API URL, to get your personal location.
- `ENTUR_STOP` The entur id of the stop for where you want to list departures.
- `ENTUR_FILTER` OPTOINAL If you only want to list specific lines, you can filter the interesting ones here.
- `GEMINI_API_KEY` Key for the Gemini API, used by the voice chat. Create one at https://aistudio.google.com/apikey.
- `GEMINI_MODEL` OPTIONAL Gemini Live model, `gemini-3.8-live` by default.

### Start the server

```sh
npm start
```

The API listens on port 3000 by default.

## Voice chat

The voice screen streams microphone audio over a WebSocket (`/api/v1/voice`) to the server, which relays it to the [Gemini Live API](https://ai.google.dev/gemini-api/docs/live-api). Gemini listens, answers in Norwegian with Google Search when it needs current facts, and streams speech back. It also calls a `set_mood` tool, which changes the face on the screen. Personalities are defined in `server/services/personas.ts`; each one picks a face that the firmware knows how to draw (`waifu` or `oracle`).

Audio is 16-bit mono PCM at 16 kHz in both directions; the server resamples Gemini's 24 kHz speech. As in Waveshare's XiaoZhi firmware, the ES7210 records the microphone and a hardware loopback of the speaker in TDM mode, and ESP-SR's echo canceller removes the speaker from the microphone signal. The microphone therefore stays open while the persona talks, and you can interrupt it.

To test without the board, open `http://localhost:3000/voice.html` in a browser, enter the API key, and press Start. The page uses AudioWorklet to send and play audio the same way the board does.

If the server runs behind a reverse proxy, the proxy must pass WebSocket upgrades through.

## Build and upload the firmware

Install the PlatformIO CLI. Copy `firmware/include/secrets.example.h` to `firmware/include/secrets.h` and set the Wi-Fi credentials, matching `API_KEY`, and `SERVER_URL` to the server's address wherever you run the server. The voice screen uses the same server; set `VOICE_URL` only if it lives somewhere else.

```sh
cd firmware
pio run
pio run -t upload
```

If pio is not in your PATH and you won't bother (just as me) you can just invoke it via python instead:

```sh
cd firmware
python -m platformio run
python -m platformio run -t upload
```

## Deploy the server with Docker

The GitHub Actions workflow is intended to build the server image and publish it to `ghcr.io/lebbe/rekkeru:latest` when changes under `server/` are pushed to `main`. The container listens on port 3000. On the host, the deploy script runs it bound to `127.0.0.1:3000`, so put a reverse proxy in front of it if it needs to be reachable from outside the host.

To deploy from your development machine, configure `DEPLOY_HOST` in `server/.env` as an SSH destination, make sure Docker and SSH key authentication are set up on that host, and create the remote deployment directory (`~/rekkeru`). Then run from `server/`:

```sh
npm run deploy
```

The host's `.env` is copied from `server/.env`. Keep it private; it contains the API key and calendar-feed address. The image name in `server/deploy.sh` must match the published GHCR image before deploying.

THIS WILL DEPLOY THE DOCKER IMAGE BUILT FOR THE OFFICIAL [REKKERU](https://github.com/lebbe/rekkeru) REPOSITORY. If you create a fork of this repo, and want to deploy your own version, you need to adjust docker.yaml etc accordingly.
