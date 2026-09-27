import ical from 'node-ical'
import type { EventInstance, VEvent } from 'node-ical'

const TZ = 'Europe/Oslo'
const DAY = 86_400_000
let cache: { events: VEvent[]; expires: number } = { events: [], expires: 0 }

async function fetchCalendar(): Promise<string[]> {
  if (Date.now() >= cache.expires) {
    const res = await fetch(process.env.SECRET_ICAL_ADDRESS!)
    if (!res.ok) throw new Error(`Kalenderen svarte ${res.status}`)
    const parsed = await ical.async.parseICS(await res.text())
    cache = {
      events: Object.values(parsed).filter(
        (event): event is VEvent => event?.type === 'VEVENT',
      ),
      expires: Date.now() + 5 * 60_000,
    }
  }

  const now = new Date()
  const upcoming: EventInstance[] = cache.events
    .flatMap((event) =>
      ical.expandRecurringEvent(event, {
        from: now,
        to: new Date(+now + 60 * DAY),
      }),
    )
    .filter((event) => event.start > now)
    .sort((a, b) => a.start.getTime() - b.start.getTime())
    .slice(0, 4)

  const fmt = (date: Date, options: Intl.DateTimeFormatOptions) =>
    date
      .toLocaleString('nb-NO', { timeZone: TZ, ...options })
      .replace(/\.$/, '')
  return upcoming.map((event) => {
    const sameDay =
      fmt(event.start, { dateStyle: 'short' }) ===
      fmt(now, { dateStyle: 'short' })
    const day = sameDay
      ? ''
      : fmt(
          event.start,
          event.start.getTime() - now.getTime() < 6 * DAY
            ? { weekday: 'short' }
            : { day: 'numeric', month: 'numeric' },
        )
    const time = event.isFullDay
      ? ''
      : fmt(event.start, { hour: '2-digit', minute: '2-digit' })
    const summary =
      typeof event.summary === 'string' ? event.summary : event.summary.val
    return [day, time, summary].filter(Boolean).join(' ')
  })
}

export default async function getCalendar(): Promise<
  string[] | { error: string }
> {
  try {
    return await fetchCalendar()
  } catch (err) {
    console.error('Kalender:', err)
    return { error: 'Kalender utilgjengelig' }
  }
}
