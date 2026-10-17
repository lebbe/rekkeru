const UA = process.env.UA_STRING
const WEATHER_QUERY = process.env.WEATHER_QUERY
const URL = `https://api.met.no/weatherapi/locationforecast/2.0/compact?${WEATHER_QUERY}`

const TEXT = {
  clearsky: 'Klarvær',
  fair: 'Lettskyet',
  partlycloudy: 'Delvis skyet',
  cloudy: 'Skyet',
  fog: 'Tåke',
} as const

const PRECIP = { rain: 'Regn', sleet: 'Sludd', snow: 'Snø' } as const
type Precipitation = keyof typeof PRECIP
type WeatherIcon =
  | 'thunder'
  | 'sleet'
  | 'snow'
  | 'rain'
  | 'fog'
  | 'clear_night'
  | 'clear'
  | 'partly_night'
  | 'partly'
  | 'cloudy'
type WeatherResponse = {
  properties: {
    timeseries: {
      time: string
      data: {
        next_1_hours?: { summary: { symbol_code: string } }
        instant: { details: { air_temperature: number } }
      }
    }[]
  }
}
type WeatherNow = { temp: number; icon: WeatherIcon; text: string | undefined }
type WeatherResult =
  | (WeatherNow & { forecast: (WeatherNow & { time: number })[] })
  | { error: string }

let cache: { data: WeatherResponse | null; expires: number } = {
  data: null,
  expires: 0,
}

function toIcon(code: string): WeatherIcon {
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

function getPrecipitation(code: string): Precipitation | undefined {
  if (code.includes('rain')) return 'rain'
  if (code.includes('sleet')) return 'sleet'
  if (code.includes('snow')) return 'snow'
}

async function fetchWeather(): Promise<WeatherResult> {
  if (Date.now() >= cache.expires) {
    const res = await fetch(URL, {
      headers: UA ? { 'User-Agent': UA } : {},
    }).catch(() => null)
    cache.expires =
      Date.parse(res?.headers.get('expires') ?? '') || Date.now() + 10 * 60_000
    if (res?.ok) cache.data = (await res.json()) as WeatherResponse
  }
  if (!cache.data) throw new Error('Ingen værdata ennå')

  const day = (time: string) =>
    new Date(time).toLocaleDateString('sv-SE', { timeZone: 'Europe/Oslo' })
  const symbol = (entry: WeatherResponse['properties']['timeseries'][number]) =>
    entry.data.next_1_hours?.summary.symbol_code ?? ''
  const series = cache.data.properties.timeseries
  const index = Math.max(
    0,
    series.findLastIndex((entry) => Date.parse(entry.time) <= Date.now()),
  )
  if (!series[index]) throw new Error('Ingen værdata ennå')

  // The board only fetches once a day, so describe each of the next 24 hours as if it were "now".
  const describe = (at: number): WeatherNow => {
    const now = series[at]!
    const wet = series
      .slice(at)
      .find(
        (entry) =>
          day(entry.time) === day(now.time) &&
          getPrecipitation(symbol(entry)) !== undefined,
      )

    let text: string | undefined =
      TEXT[symbol(now).replace(/_.*/, '') as keyof typeof TEXT]
    if (wet) {
      const kind = getPrecipitation(symbol(wet))
      if (kind) {
        const hour = new Date(wet.time).toLocaleTimeString('nb-NO', {
          timeZone: 'Europe/Oslo',
          hour: '2-digit',
        })
        text =
          wet === now ? `${PRECIP[kind]} nå` : `${PRECIP[kind]} fra kl. ${hour}`
      }
    }
    return {
      temp: Math.round(now.data.instant.details.air_temperature),
      icon: toIcon(symbol(now)),
      text,
    }
  }

  return {
    ...describe(index),
    forecast: series.slice(index, index + 24).map((entry, offset) => ({
      time: Math.floor(Date.parse(entry.time) / 1000),
      ...describe(index + offset),
    })),
  }
}

export async function getWeather(): Promise<WeatherResult> {
  try {
    return await fetchWeather()
  } catch (err) {
    console.error('Vær:', err)
    return { error: 'Værdata mangler' }
  }
}
