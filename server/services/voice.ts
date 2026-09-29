// Voice chat relay: the board (or the test page in public/) talks to this WebSocket, and the
// server talks to the Gemini Live API. The board only plays and records raw audio; everything
// else (personas, prompts, search, mood) is decided here.
//
// Board -> server:  binary  PCM16 mono 16 kHz microphone audio (echo already removed), any chunk size
//                   text    {"type":"next"} switches to the next persona
// Server -> board:  binary  PCM16 mono 16 kHz audio to play
//                   text    {"type":"persona","index":0,"name":"Yuki","face":"waifu","mood":"happy"}
//                           {"type":"state","state":"connecting"|"listening"|"hearing"|"thinking"}
//                           {"type":"mood","mood":"sad"}
//                           {"type":"interrupted"}  stop playback and drop buffered audio
//                           {"type":"error","text":"..."}

import {
  GoogleGenAI,
  Modality,
  Type,
  type LiveServerMessage,
  type Session,
} from '@google/genai'
import type { WebSocket } from 'ws'
import { MOODS, PERSONAS, systemPrompt, type Mood } from './personas.ts'

const MODEL = process.env.GEMINI_MODEL || 'gemini-3.8-live'
// Microphone RMS (PCM16) above which we show "hearing". Only affects the face, not what Gemini hears.
const SPEECH_RMS = Number(process.env.VOICE_SPEECH_RMS) || 700
const AUDIO_CHUNK = 3200 // 100 ms at 16 kHz
const THINKING_AFTER_MS = 700
const THINKING_MAX_MS = 10_000

const ai = process.env.GEMINI_API_KEY
  ? new GoogleGenAI({ apiKey: process.env.GEMINI_API_KEY })
  : null

type State = 'connecting' | 'listening' | 'hearing' | 'thinking'

const setMoodTool = {
  name: 'set_mood',
  description:
    'Endrer ansiktsuttrykket som vises på skjermen. Kall denne når følelsen din endrer seg.',
  parameters: {
    type: Type.OBJECT,
    properties: { mood: { type: Type.STRING, enum: [...MOODS] } },
    required: ['mood'],
  },
}

// Gemini speaks at 24 kHz; the board plays 16 kHz (the rate its echo canceller needs). Every three
// input samples become two, after a [1 2 1] low-pass that removes most of what would alias from
// 8 to 12 kHz. Keeps leftover samples between chunks so the boundaries do not click.
function downsampler() {
  let rest: number[] = []
  let previous = 0
  return (pcm: Buffer): Buffer => {
    const x = rest
    for (let i = 0; i + 1 < pcm.length; i += 2) x.push(pcm.readInt16LE(i))
    const groups = Math.max(0, Math.floor((x.length - 1) / 3)) // Needs one sample of look-ahead.
    const smooth = (i: number) => ((i > 0 ? x[i - 1]! : previous) + 2 * x[i]! + x[i + 1]!) / 4
    const out = Buffer.alloc(groups * 4)
    for (let g = 0; g < groups; g++) {
      const i = 3 * g
      out.writeInt16LE(Math.round(smooth(i)), g * 4)
      out.writeInt16LE(Math.round((smooth(i + 1) + smooth(i + 2)) / 2), g * 4 + 2)
    }
    if (groups > 0) previous = x[groups * 3 - 1]!
    rest = x.slice(groups * 3)
    return out
  }
}

function rms(pcm: Buffer): number {
  let sum = 0
  const n = pcm.length >> 1
  for (let i = 0; i < n; i++) sum += pcm.readInt16LE(i * 2) ** 2
  return n ? Math.sqrt(sum / n) : 0
}

export function handleVoice(ws: WebSocket, requestedPersona: number) {
  let index =
    Number.isInteger(requestedPersona) && PERSONAS[requestedPersona]
      ? requestedPersona
      : 0
  let session: Session | null = null
  let generation = 0 // Increases on every new Gemini session, so stale callbacks are ignored.
  let resumeHandle: string | undefined
  let reconnects = 0
  let closed = false
  let state: State = 'connecting'
  let lastSpeech = 0
  let hearingSince = 0
  let downsample = downsampler()

  const send = (message: object) => {
    if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(message))
  }
  const setState = (next: State) => {
    if (next === state) return
    state = next
    send({ type: 'state', state })
  }

  function fail(text: string) {
    console.error('Tale:', text)
    send({ type: 'error', text })
    ws.close()
  }

  async function connect(resume: boolean) {
    if (!ai) return fail('Mangler GEMINI_API_KEY')
    const persona = PERSONAS[index]!
    const id = ++generation
    session?.close()
    session = null
    if (!resume) resumeHandle = undefined
    setState('connecting')

    try {
      const next = await ai.live.connect({
        model: MODEL,
        config: {
          responseModalities: [Modality.AUDIO],
          // Native audio models choose the language themselves; the prompt asks for Norwegian.
          speechConfig: {
            voiceConfig: { prebuiltVoiceConfig: { voiceName: persona.voice } },
          },
          systemInstruction: systemPrompt(persona),
          tools: [
            { googleSearch: {} },
            { functionDeclarations: [setMoodTool] },
          ],
          inputAudioTranscription: {},
          outputAudioTranscription: {},
          contextWindowCompression: { slidingWindow: {} },
          sessionResumption: resumeHandle ? { handle: resumeHandle } : {},
        },
        callbacks: {
          onmessage: (message) => id === generation && onMessage(message),
          onerror: (e) => console.error('Gemini:', e.message),
          onclose: (e) => {
            if (id !== generation || closed) return
            console.log(`Gemini lukket (${e.code} ${e.reason})`)
            // Sessions end after a while (see goAway); resume so the conversation continues.
            if (reconnects++ < 3) setTimeout(() => connect(true), 500)
            else fail('Mistet kontakten med Gemini')
          },
        },
      })
      if (id !== generation || closed) return next.close()
      session = next
      setState('listening')
      if (!resume) {
        // Let the persona greet the user, which also confirms that everything works.
        session.sendRealtimeInput({
          text: '(Brukeren har nettopp slått deg på. Hils kort, i rollen din.)',
        })
      }
    } catch (err) {
      if (id === generation) fail(`Kunne ikke koble til Gemini: ${err}`)
    }
  }

  function onMessage(message: LiveServerMessage) {
    const content = message.serverContent
    for (const part of content?.modelTurn?.parts ?? []) {
      const data = part.inlineData?.data
      if (data && ws.readyState === ws.OPEN) {
        // Small messages, so the board never needs a large receive buffer.
        const pcm = downsample(Buffer.from(data, 'base64'))
        for (let i = 0; i < pcm.length; i += AUDIO_CHUNK) {
          ws.send(pcm.subarray(i, i + AUDIO_CHUNK))
        }
        reconnects = 0
      }
    }
    if (content?.modelTurn) setState('listening')
    // You talked over the persona: the board drops the rest of what it was going to say.
    if (content?.interrupted) {
      downsample = downsampler()
      send({ type: 'interrupted' })
    }
    if (content?.inputTranscription?.text)
      console.log('Bruker:', content.inputTranscription.text)
    if (content?.outputTranscription?.text)
      console.log(`${PERSONAS[index]!.name}:`, content.outputTranscription.text)

    for (const call of message.toolCall?.functionCalls ?? []) {
      const mood = call.args?.mood as Mood | undefined
      if (call.name === 'set_mood' && mood && MOODS.includes(mood)) {
        send({ type: 'mood', mood })
      }
      session?.sendToolResponse({
        functionResponses: [
          { id: call.id, name: call.name, response: { output: 'ok' } },
        ],
      })
    }

    const update = message.sessionResumptionUpdate
    if (update?.resumable && update.newHandle) resumeHandle = update.newHandle
    if (message.goAway) connect(true)
  }

  function onAudio(pcm: Buffer) {
    const now = Date.now()
    if (rms(pcm) > SPEECH_RMS) {
      lastSpeech = now
      if (state === 'listening') {
        hearingSince = now
        setState('hearing')
      }
    } else if (state === 'hearing' && now - lastSpeech > THINKING_AFTER_MS) {
      setState('thinking')
    } else if (state === 'thinking' && now - lastSpeech > THINKING_MAX_MS) {
      setState('listening')
    }
    if (state === 'hearing' && now - hearingSince > 30_000) setState('listening')

    session?.sendRealtimeInput({
      audio: {
        data: pcm.toString('base64'),
        mimeType: 'audio/pcm;rate=16000',
      },
    })
  }

  function sendPersona() {
    const persona = PERSONAS[index]!
    send({
      type: 'persona',
      index,
      name: persona.name,
      face: persona.face,
      mood: persona.mood,
    })
  }

  ws.on('message', (data, isBinary) => {
    if (isBinary) {
      const pcm = Buffer.isBuffer(data)
        ? data
        : Buffer.concat(Array.isArray(data) ? data : [Buffer.from(data)])
      if (pcm.length >= 2) onAudio(pcm.subarray(0, pcm.length & ~1))
      return
    }
    let message: { type?: string }
    try {
      message = JSON.parse(data.toString())
    } catch {
      return
    }
    if (message.type === 'next') {
      index = (index + 1) % PERSONAS.length
      sendPersona()
      connect(false)
    }
  })

  // Keep NAT and reverse proxies from closing the connection while nobody is talking.
  const ping = setInterval(() => ws.ping(), 20_000)
  ws.on('close', () => {
    closed = true
    clearInterval(ping)
    session?.close()
    session = null
  })

  console.log(`Tale: ny tilkobling, ${PERSONAS[index]!.name}`)
  sendPersona()
  connect(false)
}
