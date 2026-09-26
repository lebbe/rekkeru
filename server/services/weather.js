const URL =
  'https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=59.874&lon=10.811&altitude=150'
const UA = process.env.UA_STRING

const TEXT = {
  clearsky: 'Klarvær',
  fair: 'Lettskyet',
  partlycloudy: 'Delvis skyet',
  cloudy: 'Skyet',
  fog: 'Tåke',
}

const PRECIP = { rain: 'Regn', sleet: 'Sludd', snow: 'Snø' }
let cache = { data: null, expires: 0 }

// Map MET symbol codes (e.g. "lightsnowshowers_night") to the icon drawn by the board.
function toIcon(code) {
  const night = code.endsWith('_night')
  if (code.includes('thunder')) return 'thunder'
  if (code.includes('sleet')) return 'sleet'
  if (code.includes('snow')) return 'snow'
  if (code.includes('rain')) return 'rain'
  if (code.startsWith('fog')) return 'fog'
  if (code.startsWith('clearsky')) return night ? 'clear_night' : 'clear'
  if (code.startsWith('fair') || code.startsWith('partlycloudy'))
    return night ? 'partly_night' : 'partly'
  return 'cloudy'
}

async function fetchWeather() {
  if (Date.now() >= cache.expires) {
    const res = await fetch(URL, { headers: { 'User-Agent': UA } }).catch(
      () => null,
    )
    cache.expires =
      Date.parse(res?.headers.get('expires')) || Date.now() + 10 * 60_000
    if (res?.ok) cache.data = await res.json()
  }
  if (!cache.data) throw new Error('Ingen værdata ennå')

  const day = (t) =>
    new Date(t).toLocaleDateString('sv-SE', { timeZone: 'Europe/Oslo' })
  const symbol = (e) => e.data.next_1_hours?.summary.symbol_code ?? ''
  const series = cache.data.properties.timeseries
  const i = Math.max(
    0,
    series.findLastIndex((e) => Date.parse(e.time) <= Date.now()),
  )
  const now = series[i]
  const wet = series
    .slice(i)
    .find(
      (e) =>
        day(e.time) === day(Date.now()) && /rain|sleet|snow/.test(symbol(e)),
    )

  let text = TEXT[symbol(now).replace(/_.*/, '')]
  if (wet) {
    const kind = PRECIP[symbol(wet).match(/rain|sleet|snow/)[0]]
    const hour = new Date(wet.time).toLocaleTimeString('nb-NO', {
      timeZone: 'Europe/Oslo',
      hour: '2-digit',
    })
    text = wet === now ? `${kind} nå` : `${kind} fra kl. ${hour}`
  }
  return {
    temp: Math.round(now.data.instant.details.air_temperature),
    icon: toIcon(symbol(now)),
    text,
  }
}

export async function getWeather() {
  try {
    return await fetchWeather()
  } catch (err) {
    console.error('Vær:', err)
    return { error: 'Værdata mangler' }
  }
}
