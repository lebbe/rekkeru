const STOP = process.env.ENTUR_STOP
const FILTER = process.env.ENTUR_FILTER || ''
const regexFilter = new RegExp(FILTER, 'g')

const QUERY = `query ($id: String!, $start: DateTime!) {
  stopPlace(id: $id) {
    estimatedCalls(startTime: $start, numberOfDepartures: 20, whiteListedModes: [metro]) {
      expectedDepartureTime
      destinationDisplay { frontText }
      serviceJourney { line { publicCode } }
    }
  }
}`

type Departure = { line: string; dest: string; time: number }
type DepartureResult = Departure[] | { error: string }
type EnturResponse = {
  data?: {
    stopPlace?: {
      estimatedCalls: {
        expectedDepartureTime: string
        destinationDisplay: { frontText: string }
        serviceJourney: { line: { publicCode: string } }
      }[]
    }
  }
}

async function fetchDepartures(
  now = Math.floor(Date.now() / 1000),
): Promise<Departure[]> {
  const res = await fetch('https://api.entur.io/journey-planner/v3/graphql', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'ET-Client-Name': process.env.UA_STRING || 'mindless developers',
    },
    body: JSON.stringify({
      query: QUERY,
      variables: { id: STOP, start: new Date(now * 1000).toISOString() },
    }),
  })
  const { data } = (await res.json()) as EnturResponse
  if (!data?.stopPlace)
    throw new Error(`Fant ingen avganger (Entur svarte ${res.status})`)

  return data.stopPlace.estimatedCalls
    .map((call) => ({
      line: call.serviceJourney.line.publicCode.padStart(2, '0'),
      dest: call.destinationDisplay.frontText,
      time: Math.floor(Date.parse(call.expectedDepartureTime) / 1000),
    }))
    .filter((departure) => departure.dest.match(regexFilter))
}

export default async function getDepartures(
  now: number,
): Promise<DepartureResult> {
  try {
    return await fetchDepartures(now)
  } catch (err) {
    console.error('Avganger:', err)
    return { error: 'Fikk ikke hentet T-banetider' }
  }
}
