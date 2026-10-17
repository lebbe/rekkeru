import ical from 'node-ical'
import type { EventInstance, VEvent } from 'node-ical'

const DAY = 86_400_000
let cache: { events: VEvent[]; expires: number } = { events: [], expires: 0 }

type CalendarEvent = {
  start: number
  end: number
  allDay: boolean
  title: string
}

async function fetchCalendar(): Promise<CalendarEvent[]> {
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
  // The board only fetches once a day, so send raw events and let it format and filter them
  // as time passes. `end > now` keeps events that are ongoing or all-day today.
  return cache.events
    .flatMap((event) =>
      ical.expandRecurringEvent(event, {
        from: now,
        to: new Date(+now + 60 * DAY),
      }),
    )
    .filter((event) => event.end > now)
    .sort((a, b) => a.start.getTime() - b.start.getTime())
    .slice(0, 10)
    .map((event) => ({
      start: Math.floor(event.start.getTime() / 1000),
      end: Math.floor(event.end.getTime() / 1000),
      allDay: event.isFullDay,
      title:
        typeof event.summary === 'string' ? event.summary : event.summary.val,
    }))
}

export default async function getCalendar(): Promise<
  CalendarEvent[] | { error: string }
> {
  try {
    return await fetchCalendar()
  } catch (err) {
    console.error('Kalender:', err)
    return { error: 'Kalender utilgjengelig' }
  }
}
