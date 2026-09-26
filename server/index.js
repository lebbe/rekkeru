import crypto from 'node:crypto'
import express from 'express'
import getDepartures from './services/departures.js'
import { getWeather } from './services/weather.js'
import getCalendar from './services/calendar.js'

const configuredToken = process.env.API_KEY

if (!configuredToken || !/^[a-fA-F0-9]{64}$/.test(configuredToken)) {
  throw new Error('API_KEY must be set to a 64-character hexadecimal token')
}

const expectedToken = Buffer.from(configuredToken, 'hex')
const app = express()

app.get('/api/v1/screen', async (req, res) => {
  const authorization = req.get('authorization') || ''
  const match = /^Bearer ([a-fA-F0-9]{64})$/i.exec(authorization)

  if (!match) {
    return res.status(418).send("I'm a tea pot.").end()
  }

  const suppliedToken = Buffer.from(match[1], 'hex')
  if (!crypto.timingSafeEqual(suppliedToken, expectedToken)) {
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

const port = Number(process.env.PORT) || 3000
app.listen(port, () => {
  console.log(`Screen API listening on port ${port}`)
})
