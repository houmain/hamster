#!/usr/bin/env node
'use strict'

const fs = require('fs')
const http = require('http')
const os = require('os')
const path = require('path')
const readline = require('readline')
const { spawn } = require('child_process')
const { createHash, X509Certificate } = require('crypto')

const repositoryRoot = path.resolve(__dirname, '..')

function usage () {
  console.log(`Usage: node tools/record.js <url> [options]

Options:
  --archive <path>       Relative archive path inside the library.
  --library-root <dir>   Isolated library/catalogue directory.
  --ca-dir <dir>         Hamster CA directory (defaults beside hamster).
  --hamster <file>       Hamster executable.
  --browser <file>       Edge, Chrome, or Chromium executable.
  --timeout <seconds>    Page-load timeout (default 90).
  --headless             Run Chromium in headless mode.
  --no-validate          Do not replay and validate the completed archive.
  --keep-profile         Keep the temporary browser profile.
  -h, --help             Show this help.

When --library-root is omitted, a new directory under the system temporary
directory is used and retained so the recorded archive remains available.`)
}

function parseArguments (arguments_) {
  const result = { timeout: 90, headless: false, keepProfile: false, validate: true }
  const positional = []
  const values = new Map([
    ['--archive', 'archive'],
    ['--library-root', 'libraryRoot'],
    ['--ca-dir', 'caDir'],
    ['--hamster', 'hamster'],
    ['--browser', 'browser'],
    ['--timeout', 'timeout']
  ])
  for (let i = 0; i < arguments_.length; ++i) {
    const argument = arguments_[i]
    if (argument === '-h' || argument === '--help') {
      result.help = true
    } else if (argument === '--headless') {
      result.headless = true
    } else if (argument === '--no-validate') {
      result.validate = false
    } else if (argument === '--keep-profile') {
      result.keepProfile = true
    } else if (values.has(argument)) {
      if (++i === arguments_.length) throw new Error(`Missing value for ${argument}`)
      result[values.get(argument)] = arguments_[i]
    } else if (argument.startsWith('-')) {
      throw new Error(`Unknown option: ${argument}`)
    } else {
      positional.push(argument)
    }
  }
  if (result.help) return result
  if (positional.length !== 1) throw new Error('Expected exactly one URL')
  result.url = new URL(positional[0])
  if (!['http:', 'https:'].includes(result.url.protocol))
    throw new Error('The recording URL must use HTTP or HTTPS')
  result.timeout = Number(result.timeout)
  if (!Number.isFinite(result.timeout) || result.timeout <= 0)
    throw new Error('--timeout must be a positive number')
  return result
}

function existingFile (candidates) {
  for (const candidate of candidates) {
    if (!candidate) continue
    try {
      fs.accessSync(candidate, fs.constants.X_OK)
      return path.resolve(candidate)
    } catch {}
  }
}

function newestExistingFile (candidates) {
  return candidates
    .map(candidate => {
      try {
        fs.accessSync(candidate, fs.constants.X_OK)
        return { path: path.resolve(candidate), modified: fs.statSync(candidate).mtimeMs }
      } catch {
        return null
      }
    })
    .filter(Boolean)
    .sort((a, b) => b.modified - a.modified)[0]?.path
}

function findOnPath (names) {
  const extensions = process.platform === 'win32'
    ? (process.env.PATHEXT || '.EXE;.CMD;.BAT').split(';')
    : ['']
  for (const directory of (process.env.PATH || '').split(path.delimiter)) {
    for (const name of names) {
      for (const extension of extensions) {
        const candidate = path.join(directory, name +
          (path.extname(name) ? '' : extension.toLowerCase()))
        const found = existingFile([candidate])
        if (found) return found
      }
    }
  }
}

function findHamster (configured) {
  if (configured) {
    const result = existingFile([configured])
    if (!result) throw new Error(`Hamster executable not found: ${configured}`)
    return result
  }
  const executable = process.platform === 'win32' ? 'hamster.exe' : 'hamster'
  const result = newestExistingFile([
    path.join(repositoryRoot, '_build', 'Release', executable),
    path.join(repositoryRoot, '_build', 'Debug', executable),
    path.join(repositoryRoot, '_build', executable),
    path.join(repositoryRoot, executable)
  ]) || findOnPath(['hamster'])
  if (!result) throw new Error('Hamster executable not found; use --hamster')
  return result
}

function findBrowser (configured) {
  if (configured) {
    const result = existingFile([configured])
    if (!result) throw new Error(`Browser executable not found: ${configured}`)
    return result
  }
  const candidates = []
  if (process.platform === 'win32') {
    for (const base of [process.env['ProgramFiles(x86)'], process.env.ProgramFiles,
      process.env.LOCALAPPDATA]) {
      if (!base) continue
      candidates.push(path.join(base, 'Microsoft', 'Edge', 'Application', 'msedge.exe'))
      candidates.push(path.join(base, 'Google', 'Chrome', 'Application', 'chrome.exe'))
      candidates.push(path.join(base, 'Chromium', 'Application', 'chrome.exe'))
    }
  } else if (process.platform === 'darwin') {
    candidates.push('/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge')
    candidates.push('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
    candidates.push('/Applications/Chromium.app/Contents/MacOS/Chromium')
  }
  const result = existingFile(candidates) || findOnPath([
    'msedge', 'microsoft-edge', 'microsoft-edge-stable',
    'google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'
  ])
  if (!result) throw new Error('Edge, Chrome, or Chromium not found; use --browser')
  return result
}

function defaultArchiveName (url) {
  const host = url.hostname.replace(/[^a-zA-Z0-9._-]+/g, '-') || 'recording'
  const timestamp = new Date().toISOString().replace(/[:.]/g, '-').replace('T', '_').replace('Z', '')
  return `${host}-${timestamp}.hamster`
}

function validateArchivePath (archive) {
  if (!archive || path.isAbsolute(archive) || archive.includes('\\') || archive.includes(':'))
    throw new Error('--archive must be a relative path using forward slashes')
  const parts = archive.split('/')
  if (parts.some(part => !part || part === '.' || part === '..'))
    throw new Error('--archive contains an invalid path component')
  return archive
}

function caSpkiHash (certificatePath) {
  const certificate = new X509Certificate(fs.readFileSync(certificatePath))
  const spki = certificate.publicKey.export({ type: 'spki', format: 'der' })
  return createHash('sha256').update(spki).digest('base64')
}

function postControl (port, endpoint, fields = {}) {
  const body = new URLSearchParams(fields).toString()
  return new Promise((resolve, reject) => {
    const request = http.request({
      hostname: '127.0.0.1',
      port,
      path: `/${endpoint}`,
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
        'Content-Length': Buffer.byteLength(body),
        Connection: 'close'
      }
    }, response => {
      const chunks = []
      response.on('data', chunk => chunks.push(chunk))
      response.on('end', () => resolve({
        status: response.statusCode,
        body: Buffer.concat(chunks).toString('utf8')
      }))
    })
    request.on('error', reject)
    request.end(body)
  })
}

function delay (milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds))
}

function deferred () {
  let resolve
  let reject
  const promise = new Promise((resolve_, reject_) => {
    resolve = resolve_
    reject = reject_
  })
  return { promise, resolve, reject }
}

function timeoutAfter (seconds, description) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(
      () => reject(new Error(`${description} timed out after ${seconds} seconds`)),
      seconds * 1000)
    timer.unref()
  })
}

async function finishRecording (port, archive) {
  const deadline = Date.now() + 30000
  for (;;) {
    let response
    try {
      response = await postControl(port, 'finish-recording', { path: archive })
    } catch (error) {
      if (Date.now() >= deadline) throw error
      await delay(50)
      continue
    }
    if (response.status === 200) return
    if (response.status !== 202)
      throw new Error(`Finishing recording failed (${response.status}): ${response.body}`)
    if (Date.now() >= deadline) throw new Error('Finishing recording timed out')
    await delay(50)
  }
}

function waitForExit (child, milliseconds) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return Promise.resolve(true)
  return Promise.race([
    new Promise(resolve => child.once('exit', () => resolve(true))),
    delay(milliseconds).then(() => false)
  ])
}

async function terminate (child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return
  child.kill()
  if (!(await waitForExit(child, 5000))) child.kill('SIGKILL')
  await waitForExit(child, 2000)
}

function browserArguments (profileRoot, port, spkiHash, url, headless) {
  const result = [
    `--user-data-dir=${profileRoot}`,
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-default-apps',
    '--disable-background-networking',
    '--disable-quic',
    `--proxy-server=http://127.0.0.1:${port}`,
    '--proxy-bypass-list=<-loopback>',
    `--ignore-certificate-errors-spki-list=${spkiHash}`
  ]
  if (headless) result.push('--headless=new')
  result.push(url)
  return result
}

async function validateArchive (options, hamsterPath, browserPath, caDirectory,
  spkiHash, archive, archivePath, interruption, interruptToken) {
  const validationRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hamster-validation-'))
  const profileRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hamster-validation-browser-'))
  const validationArchive = path.join(validationRoot, ...archive.split('/'))
  fs.mkdirSync(path.dirname(validationArchive), { recursive: true })
  fs.copyFileSync(archivePath, validationArchive)

  let hamster
  let browser
  let stopping = false
  const ready = deferred()
  const loaded = deferred()
  const hits = new Set()
  const misses = new Set()
  const observed = new Set()
  const forwardFailures = new Set()

  try {
    hamster = spawn(hamsterPath, [
      '--bind', '127.0.0.1',
      '--port', '0',
      '--library-root', validationRoot,
      '--ca-dir', caDirectory,
      '--proxy', '127.0.0.1:9',
      '--request-timeout', '1'
    ], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true })

    readline.createInterface({ input: hamster.stdout }).on('line', line => {
      const accepted = /^ACCEPT http:\/\/[^:]+:(\d+)$/.exec(line)
      if (accepted) ready.resolve(Number(accepted[1]))
      if (line === `PAGE_LOADED ${archive}`) loaded.resolve()
      if (line.startsWith('ARCHIVE_HIT ')) hits.add(line.slice('ARCHIVE_HIT '.length))
      if (line.startsWith('ARCHIVE_MISS ')) misses.add(line.slice('ARCHIVE_MISS '.length))
      if (line.startsWith('FORWARD_FAILED ')) {
        forwardFailures.add(line.slice('FORWARD_FAILED '.length))
        return
      }
      const resourcePrefix = `PAGE_RESOURCE ${archive} `
      if (line.startsWith(resourcePrefix)) observed.add(line.slice(resourcePrefix.length))
      console.log(`[validate] ${line}`)
    })
    readline.createInterface({ input: hamster.stderr }).on('line', line => {
      console.error(`[validate] ${line}`)
    })
    hamster.once('error', error => ready.reject(error))
    const hamsterExit = new Promise((resolve, reject) => {
      hamster.once('exit', (code, signal) => {
        if (stopping) return resolve()
        reject(new Error(`Validation Hamster exited unexpectedly (${signal || code})`))
      })
    })

    const port = await Promise.race([
      ready.promise, timeoutAfter(15, 'Starting validation Hamster'), hamsterExit, interruption
    ])
    if (port === interruptToken) throw new Error('Interrupted')

    let response = await postControl(port, 'set-recording-options', {
      serve: 'latest',
      download: 'never',
      'allow-lossy-compression': 'false'
    })
    if (response.status !== 200)
      throw new Error(`Configuring archive-only replay failed (${response.status}): ${response.body}`)
    response = await postControl(port, 'replace-bookmarks', {
      count: '1',
      'url.0': options.url.href,
      'path.0': archive
    })
    if (response.status !== 200)
      throw new Error(`Configuring validation catalogue failed (${response.status}): ${response.body}`)
    response = await postControl(port, 'get-archive-files', { path: archive })
    if (response.status !== 200)
      throw new Error(`Reading archive manifest failed (${response.status}): ${response.body}`)
    const archived = new Set(JSON.parse(response.body).files.map(file => file.url))

    browser = spawn(browserPath,
      browserArguments(profileRoot, port, spkiHash, options.url.href, options.headless),
      { stdio: 'ignore', windowsHide: false })
    const browserExit = new Promise((resolve, reject) => {
      browser.once('error', reject)
      browser.once('exit', (code, signal) => {
        if (stopping) return resolve()
        reject(new Error(`Validation browser exited before the page loaded (${signal || code})`))
      })
    })
    const completion = await Promise.race([
      loaded.promise,
      browserExit,
      hamsterExit,
      interruption,
      timeoutAfter(options.timeout, 'Validating archive')
    ])
    if (completion === interruptToken) throw new Error('Interrupted')

    stopping = true
    await terminate(browser)
    browser = null
    response = await postControl(port, 'stop').catch(() => null)
    if (response && response.status !== 200)
      throw new Error(`Stopping validation Hamster failed (${response.status}): ${response.body}`)
    if (!(await waitForExit(hamster, 30000)))
      throw new Error('Validation Hamster did not stop cleanly')
    hamster = null

    const unused = [...archived].filter(url => !hits.has(url)).sort()
    const failed = [...new Set([
      ...misses,
      ...[...observed].filter(url => !hits.has(url))
    ])].sort()
    if (failed.length || unused.length) {
      for (const url of failed) console.error(`Validation missing: ${url}`)
      for (const url of unused) console.error(`Validation unused: ${url}`)
      throw new Error(`Archive validation failed: ${failed.length} missing, ${unused.length} unused`)
    }
    console.log(`Archive validated: ${hits.size} requested and served from the archive`)
    if (forwardFailures.size)
      console.log(`Blocked ${forwardFailures.size} non-page browser network requests`)
  } finally {
    stopping = true
    await terminate(browser)
    await terminate(hamster)
    if (options.keepProfile) {
      console.log(`Validation browser profile: ${profileRoot}`)
    } else {
      fs.rmSync(profileRoot, { recursive: true, force: true })
    }
    fs.rmSync(validationRoot, { recursive: true, force: true })
  }
}

async function main () {
  const options = parseArguments(process.argv.slice(2))
  if (options.help) {
    usage()
    return
  }

  const hamsterPath = findHamster(options.hamster)
  const browserPath = findBrowser(options.browser)
  const libraryRoot = path.resolve(options.libraryRoot ||
    fs.mkdtempSync(path.join(os.tmpdir(), 'hamster-recording-')))
  const profileRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hamster-browser-'))
  const caDirectory = path.resolve(options.caDir || path.join(path.dirname(hamsterPath), 'ca'))
  const archive = validateArchivePath(options.archive || defaultArchiveName(options.url))
  const archivePath = path.join(libraryRoot, ...archive.split('/'))
  fs.mkdirSync(libraryRoot, { recursive: true })

  let hamster
  let browser
  let port
  let stopping = false
  const ready = deferred()
  const loaded = deferred()
  const interrupted = deferred()
  const interruptToken = Symbol('interrupted')
  process.once('SIGINT', () => interrupted.resolve())
  const interruption = interrupted.promise.then(() => interruptToken)

  try {
    hamster = spawn(hamsterPath, [
      '--bind', '127.0.0.1',
      '--port', '0',
      '--library-root', libraryRoot,
      '--ca-dir', caDirectory
    ], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true })

    readline.createInterface({ input: hamster.stdout }).on('line', line => {
      console.log(`[hamster] ${line}`)
      const accepted = /^ACCEPT http:\/\/[^:]+:(\d+)$/.exec(line)
      if (accepted) ready.resolve(Number(accepted[1]))
      if (line === `PAGE_LOADED ${archive}`) loaded.resolve()
    })
    readline.createInterface({ input: hamster.stderr }).on('line', line => {
      console.error(`[hamster] ${line}`)
    })
    hamster.once('error', error => ready.reject(error))
    const hamsterExit = new Promise((resolve, reject) => {
      hamster.once('exit', (code, signal) => {
        if (stopping) return resolve()
        console.error(`[hamster] exited unexpectedly (${signal || code})`)
        reject(new Error(`Hamster exited before completion (${signal || code})`))
      })
    })

    port = await Promise.race([
      ready.promise, timeoutAfter(15, 'Starting Hamster'), hamsterExit, interruption
    ])
    if (port === interruptToken) throw new Error('Interrupted')
    const certificatePath = path.join(caDirectory, 'ca.crt')
    const spkiHash = caSpkiHash(certificatePath)

    let response = await postControl(port, 'replace-bookmarks', {
      count: '1',
      'url.0': options.url.href,
      'path.0': archive
    })
    if (response.status !== 200)
      throw new Error(`Configuring recording failed (${response.status}): ${response.body}`)

    console.log(`Browser: ${browserPath}`)
    console.log(`Library: ${libraryRoot}`)
    console.log(`Archive: ${archivePath}`)
    browser = spawn(browserPath,
      browserArguments(profileRoot, port, spkiHash, options.url.href, options.headless),
      { stdio: 'ignore', windowsHide: false })
    const browserExit = new Promise((resolve, reject) => {
      browser.once('error', reject)
      browser.once('exit', (code, signal) => {
        if (stopping) return resolve()
        reject(new Error(`Browser exited before the page loaded (${signal || code})`))
      })
    })

    const completion = await Promise.race([
      loaded.promise,
      browserExit,
      hamsterExit,
      interruption,
      timeoutAfter(options.timeout, 'Loading page')
    ])
    if (completion === interruptToken) throw new Error('Interrupted')
    await finishRecording(port, archive)
    console.log(`Recording committed: ${archivePath}`)

    stopping = true
    await terminate(browser)
    browser = null
    response = await postControl(port, 'stop').catch(() => null)
    if (response && response.status !== 200)
      throw new Error(`Stopping Hamster failed (${response.status}): ${response.body}`)
    if (!(await waitForExit(hamster, 30000)))
      throw new Error('Hamster did not stop cleanly')
    hamster = null
    if (options.validate) {
      await validateArchive(options, hamsterPath, browserPath, caDirectory,
        spkiHash, archive, archivePath, interruption, interruptToken)
    }
  } finally {
    stopping = true
    await terminate(browser)
    await terminate(hamster)
    if (options.keepProfile) {
      console.log(`Browser profile: ${profileRoot}`)
    } else {
      fs.rmSync(profileRoot, { recursive: true, force: true })
    }
  }
}

main().catch(error => {
  console.error(`record: ${error.message || error}`)
  process.exitCode = 1
})
