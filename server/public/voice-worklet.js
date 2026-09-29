// AudioWorklet processors for voice.html. The AudioContext runs at 16 kHz, the same rate as the board.

const FRAME = 320 // 20 ms

class MicProcessor extends AudioWorkletProcessor {
  constructor() {
    super()
    this.frame = new Int16Array(FRAME)
    this.length = 0
  }

  process(inputs) {
    const input = inputs[0]?.[0]
    if (!input) return true
    for (const sample of input) {
      this.frame[this.length++] = Math.max(-1, Math.min(1, sample)) * 0x7fff
      if (this.length === FRAME) {
        this.port.postMessage(this.frame.buffer, [this.frame.buffer])
        this.frame = new Int16Array(FRAME)
        this.length = 0
      }
    }
    return true
  }
}

class PlayerProcessor extends AudioWorkletProcessor {
  constructor() {
    super()
    this.queue = []
    this.offset = 0
    this.playing = false
    this.port.onmessage = ({ data }) => {
      if (data === 'flush') this.queue = []
      else this.queue.push(new Int16Array(data))
    }
  }

  process(_inputs, outputs) {
    const output = outputs[0][0]
    let sum = 0
    for (let i = 0; i < output.length; i++) {
      const chunk = this.queue[0]
      if (!chunk) {
        output[i] = 0
        continue
      }
      output[i] = chunk[this.offset++] / 0x8000
      sum += output[i] * output[i]
      if (this.offset >= chunk.length) {
        this.queue.shift()
        this.offset = 0
      }
    }
    const playing = this.queue.length > 0
    if (playing !== this.playing || playing) {
      this.playing = playing
      this.port.postMessage({ playing, level: Math.sqrt(sum / output.length) })
    }
    return true
  }
}

registerProcessor('mic', MicProcessor)
registerProcessor('player', PlayerProcessor)
