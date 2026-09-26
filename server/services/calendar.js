import ical from 'node-ical'

const TZ = 'Europe/Oslo'
const DAY = 86_400_000
let cache = { events: [], expires: 0 }

async function fetchCalendar() {
  if (Date.now() >= cache.expires) {
    const res = await fetch(process.env.SECRET_ICAL_ADDRESS)
    if (!res.ok) throw new Error(`Kalenderen svarte ${res.status}`)
    const parsed = await ical.async.parseICS(await res.text())
    cache = {
      events: Object.values(parsed).filter((e) => e.type === 'VEVENT'),
      expires: Date.now() + 5 * 60_000,
    }
  }

  const now = new Date()
  const upcoming = cache.events
    .flatMap((e) =>
      ical.expandRecurringEvent(e, {
        from: now,
        to: new Date(+now + 60 * DAY),
      }),
    )
    .filter((e) => e.start > now)
    .sort((a, b) => a.start - b.start)
    .slice(0, 4)

  const fmt = (d, opts) =>
    d.toLocaleString('nb-NO', { timeZone: TZ, ...opts }).replace(/\.$/, '')
  return upcoming.map((e) => {
    const sameDay =
      fmt(e.start, { dateStyle: 'short' }) === fmt(now, { dateStyle: 'short' })
    const day = sameDay
      ? ''
      : fmt(
          e.start,
          e.start - now < 6 * DAY
            ? { weekday: 'short' }
            : { day: 'numeric', month: 'numeric' },
        )
    const time = e.isFullDay
      ? ''
      : fmt(e.start, { hour: '2-digit', minute: '2-digit' })
    return [day, time, e.summary?.val ?? e.summary].filter(Boolean).join(' ')
  })
}

export default async function getCalendar() {
  try {
    return await fetchCalendar()
  } catch (err) {
    console.error('Kalender:', err)
    return { error: 'Kalender utilgjengelig' }
  }
}
