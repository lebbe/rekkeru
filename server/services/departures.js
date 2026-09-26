const STOP = 'NSR:StopPlace:58245' // Lambertseter (bus + metro)
const QUERY = `query ($id: String!, $start: DateTime!) {
  stopPlace(id: $id) {
    estimatedCalls(startTime: $start, numberOfDepartures: 20, whiteListedModes: [metro]) {
      expectedDepartureTime
      destinationDisplay { frontText }
      serviceJourney { line { publicCode } }
    }
  }
}`

async function fetchDepartures(now = Math.floor(Date.now() / 1000)) {
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
  const { data } = await res.json()
  if (!data?.stopPlace)
    throw new Error(`Fant ingen avganger (Entur svarte ${res.status})`)

  return data.stopPlace.estimatedCalls
    .map((c) => ({
      line: c.serviceJourney.line.publicCode.padStart(2, '0'),
      dest: c.destinationDisplay.frontText,
      time: Math.floor(Date.parse(c.expectedDepartureTime) / 1000),
    }))
    .filter((d) => d.dest !== 'Bergkrystallen')
}

export default async function getDepartures(now) {
  try {
    return await fetchDepartures(now)
  } catch (err) {
    console.error('Avganger:', err)
    return { error: 'Fikk ikke hentet T-banetider' }
  }
}
