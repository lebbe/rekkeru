// Shaman drum for Alex. The drum is synthesized here (no sample files) and mixed into the speech
// stream that goes to the board, so the board only plays PCM16 mono 16 kHz as before.
//
// A pattern is rendered into one track. The track is consumed as audio is sent: while speech streams
// the track is added into it ("under"), and with "foran" the part up to the last hit is first sent on
// its own, so the drum comes before the next words. Whenever a hit starts inside a chunk, a
// {"type":"drum","delay_ms":n} message is sent just before the chunk, so the board can move the
// hand exactly when the drum is heard.

export const SAMPLE_RATE = 16_000

export const PATTERNS = ['slag', 'to_slag', 'hjerteslag', 'rulle', 'reisen'] as const
export type Pattern = (typeof PATTERNS)[number]
export const STRENGTHS = ['svak', 'middels', 'sterk'] as const
export type Strength = (typeof STRENGTHS)[number]
export const PLACEMENTS = ['foran', 'under'] as const
export type Placement = (typeof PLACEMENTS)[number]

const STRENGTH_GAIN: Record<Strength, number> = { svak: 0.2, middels: 0.4, sterk: 0.65 }
const UNDER_GAIN = 0.6 // Extra attenuation while mixed under speech.
const HIT_SECONDS = 0.8
const ATTACK_TAIL_MS = 120 // With "foran": speech may start this long after the last hit.

type Hit = { ms: number; velocity: number; pitch: number }

function patternHits(pattern: Pattern): Hit[] {
  switch (pattern) {
    case 'slag':
      return [{ ms: 0, velocity: 1, pitch: 1 }]
    case 'to_slag':
      return [
        { ms: 0, velocity: 1, pitch: 1 },
        { ms: 650, velocity: 0.7, pitch: 0.96 },
      ]
    case 'hjerteslag': {
      // Three lub-dub, a little lower than the other patterns.
      const hits: Hit[] = []
      for (let i = 0; i < 3; i++) {
        hits.push({ ms: i * 1000, velocity: 0.85, pitch: 0.88 })
        hits.push({ ms: i * 1000 + 280, velocity: 0.55, pitch: 0.84 })
      }
      return hits
    }
    case 'rulle': {
      // Accelerating roll that swells and dies away.
      const hits: Hit[] = []
      let ms = 0
      for (let i = 0; i < 20; i++) {
        const p = i / 19
        hits.push({
          ms,
          velocity: 0.25 + 0.75 * Math.sin(Math.PI * Math.min(1, p * 1.15)) ** 1.5,
          pitch: 1 - 0.04 * (i % 2),
        })
        ms += 170 - 110 * Math.sin((p * Math.PI) / 2)
      }
      return hits
    }
    case 'reisen': {
      // Steady four beats a second, the pulse used for shamanic journeys.
      const hits: Hit[] = []
      for (let i = 0; i < 20; i++) {
        hits.push({ ms: i * 250, velocity: i % 4 === 0 ? 0.9 : 0.65, pitch: 1 })
      }
      return hits
    }
  }
}

// One hit on a frame drum. The pitch falls quickly after the hit, the other membrane modes are not
// harmonics, and a short noise burst is the slap of the hand. The speaker on the board is small and
// plays little below 150 Hz, so the body is tuned high and soft-clipped to add overtones.
function renderHit(velocity: number, pitch: number): Float32Array {
  const n = Math.round(HIT_SECONDS * SAMPLE_RATE)
  const out = new Float32Array(n)
  const detune = pitch * (1 + (Math.random() - 0.5) * 0.06)
  const f0 = 150 * detune
  const modes = [
    { ratio: 1, amp: 1, decay: 0.17 },
    { ratio: 1.593, amp: 0.5, decay: 0.12 },
    { ratio: 2.136, amp: 0.32, decay: 0.09 },
    { ratio: 2.296, amp: 0.2, decay: 0.07 },
  ]
  const phases = modes.map(() => 0)
  let lowpass = 0
  for (let i = 0; i < n; i++) {
    const t = i / SAMPLE_RATE
    const fall = 1 + 0.7 * Math.exp(-t / 0.025)
    let body = 0
    modes.forEach((mode, k) => {
      phases[k]! += (2 * Math.PI * f0 * mode.ratio * fall) / SAMPLE_RATE
      body += Math.sin(phases[k]!) * mode.amp * Math.exp(-t / mode.decay)
    })
    lowpass += 0.35 * ((Math.random() * 2 - 1) - lowpass)
    const slap = lowpass * Math.exp(-t / 0.012) * 0.9
    out[i] = Math.tanh(1.6 * (body * 0.55 + slap)) * velocity
  }
  // A small room: two echoes, and the tail of the first one.
  const echoes = [
    { delay: Math.round(0.023 * SAMPLE_RATE), gain: 0.3 },
    { delay: Math.round(0.041 * SAMPLE_RATE), gain: 0.18 },
  ]
  for (let i = n - 1; i >= 0; i--) {
    for (const { delay, gain } of echoes) if (i >= delay) out[i]! += out[i - delay]! * gain
  }
  return out
}

export type Chunk = { pcm: Buffer; hitsAt: number[] } // hitsAt: ms from the start of the chunk

// Holds the pattern that is currently being played and mixes it into the audio that goes by.
export class Drummer {
  private track = new Float32Array(0)
  private hits: number[] = [] // Sample offsets into track of hits that have not started yet.
  private lastHit = 0

  get active() {
    return this.track.length > 0
  }

  stop() {
    this.track = new Float32Array(0)
    this.hits = []
  }

  // Queues a pattern after whatever remains of the current one. Returns audio to send right away
  // (for "foran"), already mixed, or an empty list.
  play(pattern: Pattern, strength: Strength, placement: Placement): Chunk[] {
    const gain = STRENGTH_GAIN[strength] * (placement === 'under' ? UNDER_GAIN : 1)
    const hits = patternHits(pattern)
    const last = hits[hits.length - 1]!
    const length = Math.round(((last.ms / 1000) + HIT_SECONDS) * SAMPLE_RATE)
    const track = new Float32Array(length)
    const onsets: number[] = []
    for (const hit of hits) {
      const start = Math.round(((hit.ms + (Math.random() - 0.5) * 24) / 1000) * SAMPLE_RATE)
      const offset = Math.max(0, start)
      const sample = renderHit(hit.velocity * (1 + (Math.random() - 0.5) * 0.2), hit.pitch)
      for (let i = 0; i < sample.length && offset + i < length; i++) {
        track[offset + i]! += sample[i]! * gain
      }
      onsets.push(offset)
    }
    this.stop()
    this.track = track
    this.hits = onsets
    this.lastHit = onsets[onsets.length - 1]!

    if (placement === 'under') return []
    // Send everything up to a moment after the last hit; the tail mixes into whatever follows.
    const solo = this.lastHit + Math.round((ATTACK_TAIL_MS / 1000) * SAMPLE_RATE)
    return this.mix(Buffer.alloc(solo * 2))
  }

  // Everything that is left, without speech. Used when the answer ends.
  drain(): Chunk[] {
    if (!this.active) return []
    return this.mix(Buffer.alloc(this.track.length * 2))
  }

  // Adds the track to `pcm` (PCM16 mono) and cuts the result into chunks of at most `size` bytes.
  mix(pcm: Buffer, size = 3200): Chunk[] {
    const chunks: Chunk[] = []
    for (let from = 0; from < pcm.length; from += size) {
      const part = Buffer.from(pcm.subarray(from, Math.min(pcm.length, from + size)))
      const hitsAt: number[] = []
      const samples = part.length >> 1
      if (this.active) {
        for (let i = 0; i < samples; i++) {
          const mixed = part.readInt16LE(i * 2) + (this.track[i] ?? 0) * 32767
          part.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(mixed))), i * 2)
        }
        while (this.hits.length && this.hits[0]! < samples) {
          hitsAt.push((this.hits.shift()! / SAMPLE_RATE) * 1000)
        }
        this.hits = this.hits.map((offset) => offset - samples)
        this.track = this.track.subarray(Math.min(samples, this.track.length))
        if (this.track.length === 0) this.stop()
      }
      chunks.push({ pcm: part, hitsAt })
    }
    return chunks
  }
}
