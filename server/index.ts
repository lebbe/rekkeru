import crypto from 'node:crypto'
import { fileURLToPath } from 'node:url'
import express from 'express'
import { WebSocketServer } from 'ws'
import getDepartures from './services/departures.ts'
import { getWeather } from './services/weather.ts'
import getCalendar from './services/calendar.ts'
import { handleVoice } from './services/voice.ts'

const configuredToken = process.env.API_KEY

if (!configuredToken || !/^[a-fA-F0-9]{64}$/.test(configuredToken)) {
  throw new Error('API_KEY must be set to a 64-character hexadecimal token')
}

const expectedToken = Buffer.from(configuredToken, 'hex')
const app = express()

function isValidToken(token: string | undefined): boolean {
  if (!token || !/^[a-fA-F0-9]{64}$/.test(token)) return false
  return crypto.timingSafeEqual(Buffer.from(token, 'hex'), expectedToken)
}

app.get('/api/v1/screen', async (req, res) => {
  const authorization = req.get('authorization') || ''
  const match = /^Bearer ([a-fA-F0-9]{64})$/i.exec(authorization)

  if (!match?.[1]) {
    return res.status(418).send("I'm a tea pot.").end()
  }

  if (!isValidToken(match[1])) {
    return res.status(418).end()
  }

  const now = Math.floor(Date.now() / 1000)
  const departures = await getDepartures(now)
  const weather = await getWeather()
  const calendar = await getCalendar()

  return res.json({
    now,
    departures,
    weather,
    calendar,
  })
})

// Browser test client for the voice chat (public/voice.html). Contains no secrets.
app.use(express.static(fileURLToPath(new URL('public', import.meta.url))))

const port = Number(process.env.PORT) || 3000
const server = app.listen(port, () => {
  console.log(`Screen API listening on port ${port}`)
})

// The board sends "Authorization: Bearer <key>". Browsers cannot set headers on WebSockets,
// so the test page sends the key as a subprotocol instead: new WebSocket(url, ['rekkeru', key]).
const voiceServer = new WebSocketServer({
  noServer: true,
  handleProtocols: (protocols) => (protocols.has('rekkeru') ? 'rekkeru' : false),
})

server.on('upgrade', (req, socket, head) => {
  const url = new URL(req.url || '/', 'http://localhost')
  const bearer = /^Bearer ([a-fA-F0-9]{64})$/i.exec(req.headers.authorization || '')?.[1]
  const protocol = (req.headers['sec-websocket-protocol'] || '')
    .split(',')
    .map((p) => p.trim())
    .find((p) => /^[a-fA-F0-9]{64}$/.test(p))

  if (url.pathname !== '/api/v1/voice' || !isValidToken(bearer ?? protocol)) {
    socket.end('HTTP/1.1 418 I\'m a teapot\r\nConnection: close\r\n\r\n')
    return
  }
  voiceServer.handleUpgrade(req, socket, head, (ws) => {
    handleVoice(ws, Number(url.searchParams.get('persona') ?? 0))
  })
})
