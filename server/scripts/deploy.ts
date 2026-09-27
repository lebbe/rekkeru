import { execSync } from 'node:child_process'

const host = process.env.DEPLOY_HOST

if (!host) {
  console.error('Missing DEPLOY_HOST in .env')
  process.exit(1)
}

const run = (cmd: string) => execSync(cmd, { stdio: 'inherit' })

run(`scp .env deploy.sh ${host}:rekkeru/`)
run(`ssh ${host} bash rekkeru/deploy.sh`)
