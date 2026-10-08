'use strict'
/* global browser, Utils, DEBUG_LOG_ASYNC_CALLS */

class Backend {
  constructor (nativeClient) {
    this._nativeClient = nativeClient
    this._server = null
    this._configuration = null
    this._proxy = null
    this._connectionRevision = 0
    this._connectionCheck = null
    this._lastConnectionStatus = null
    this._catalogUpdate = Promise.resolve()
    this.onStatus = new Set()
  }

  async configuration () {
    if (!this._configuration) {
      this._configuration = (async () => {
        const previous = await Utils.getSetting('remote-address', '')
        const previousAddress = await Utils.getSetting(
          'server-address', previous || 'http://127.0.0.1:8080')
        const previousUrl = new URL(previousAddress.includes('://')
          ? previousAddress
          : 'http://' + previousAddress)
        const hostname = await Utils.getSetting(
          'server-hostname', previousUrl.hostname || '127.0.0.1')
        const port = await Utils.getSetting('server-port', previousUrl.port || 8080)
        this._server = this._parseServer(hostname, port)
      })()
    }
    return this._configuration
  }

  _parseServer (hostname, port) {
    hostname = String(hostname).trim() || '127.0.0.1'
    port = String(port).trim() || '8080'
    if (hostname.includes('://') || hostname.includes('/') || !/^\d{1,5}$/.test(port) ||
        Number(port) < 1 || Number(port) > 65535) {
      throw new Error('Enter a hostname or IP address and a port between 1 and 65535')
    }
    const authority = hostname.includes(':') && !hostname.startsWith('[') ? `[${hostname}]` : hostname
    const url = new URL(`http://${authority}:${port}`)
    if (url.protocol !== 'http:' || url.username || url.password || url.search || url.hash || url.pathname !== '/') {
      throw new Error('Enter a hostname or IP address and a port between 1 and 65535')
    }
    return { origin: url.origin, host: url.hostname, hostname, port: Number(port) }
  }

  _isLocal (server = this._server) {
    return ['127.0.0.1', 'localhost'].includes(server?.host?.toLowerCase())
  }

  get isLocal () { return this._isLocal() }
  get serverAddress () { return this._server?.origin || 'http://127.0.0.1:8080' }
  get serverHostname () { return this._server?.hostname || '127.0.0.1' }
  get serverPort () { return this._server?.port || 8080 }
  get proxyInfo () { return this._proxy }
  get lastConnectionStatus () { return this._lastConnectionStatus }
  isControlUrl (url) { return !!this._server && new URL(url).origin === this._server.origin }

  async setConnection (hostname, port) {
    await this.configuration()
    const server = this._parseServer(hostname, port)
    const changed = server.origin !== this._server?.origin
    await Utils.setSetting('server-hostname', server.hostname)
    await Utils.setSetting('server-port', server.port)
    if (!changed) return
    this._nativeClient.disconnect()
    this._server = server
    this._proxy = null
    this._lastConnectionStatus = null
    ++this._connectionRevision
    this._configuration = Promise.resolve()
  }

  async _control (endpoint, fields = {}, timeoutMs = 10000, server) {
    await this.configuration()
    server = server || this._server
    const abort = new AbortController()
    const timeout = setTimeout(() => abort.abort(), timeoutMs)
    try {
      const response = await fetch(server.origin + '/' + endpoint, {
        method: 'POST',
        body: new URLSearchParams(fields), credentials: 'omit', cache: 'no-store', redirect: 'error', signal: abort.signal
      })
      const text = await response.text()
      if (!response.ok) throw new Error('Hamster: ' + text)
      return { status: response.status, text }
    } finally { clearTimeout(timeout) }
  }

  _compatibleVersion (value) {
    const match = /^(\d+)\.(\d+)/.exec(value || '')
    return match ? `${match[1]}.${match[2]}` : undefined
  }

  async _probe (server, timeoutMs) {
    const response = await this._control('status', {}, timeoutMs, server)
    const port = Number(response.text)
    if (!Number.isInteger(port) || port < 1 || port > 65535)
      throw new Error('Invalid Hamster proxy port')
    try {
      const version = (await this._control('version', {}, timeoutMs, server)).text.trim()
      return { port, version }
    } catch (error) {
      return { port, versionError: error.message || String(error) }
    }
  }

  _startLocal (server) {
    const quote = value => '"' + value.replace(/(\\*)"/g, '$1$1\\"').replace(/(\\+)$/, '$1$1') + '"'
    const arguments_ = ['--bind', '127.0.0.1', '--port', String(server.port)]
    return this._nativeClient.start(arguments_.map(quote).join(' '))
  }

  async _waitForLocalStart (server, disconnected) {
    const deadline = Date.now() + 5000
    let finished = false
    const poll = async () => {
      while (!finished && Date.now() < deadline) {
        try { return await this._probe(server, 500) } catch { await Utils.sleep(50) }
      }
      throw new Error('The local Hamster proxy did not start')
    }
    try {
      return await Promise.race([
        poll(),
        disconnected.then(error => {
          finished = true
          throw error
        })
      ])
    } catch (error) {
      this._nativeClient.disconnect()
      throw error
    } finally {
      finished = true
    }
  }

  async _checkConnection (server) {
    const manifest = await browser.runtime.getManifest()
    const platform = await browser.runtime.getPlatformInfo()
    const downloadSupported = ['linux', 'win', 'mac'].includes(platform.os) &&
      platform.arch === 'x86-64'
    const local = this._isLocal(server)
    const requiredVersion = this._compatibleVersion(manifest.version)
    const base = {
      local, downloadSupported, os: platform.os, arch: platform.arch,
      requiredVersion, releaseVersion: manifest.version, origin: server.origin,
      canDownload: downloadSupported && local
    }

    // Every open browser keeps a native-host lease, even when another browser
    // already owns the proxy. Standby hosts take over under the process lock.
    let disconnected
    if (local) {
      if ((await browser.windows.getAll()).length) {
        try { disconnected = this._startLocal(server) } catch {}
      } else {
        this._nativeClient.disconnect()
      }
    }

    let probe
    try {
      probe = await this._probe(server, 1000)
    } catch (probeError) {
      if (!local) {
        return { ...base, state: 'unavailable', valid: false,
          errorMessage: 'notification_no_server', detail: probeError.message || String(probeError) }
      }
      if (!downloadSupported) {
        return { ...base, state: 'unsupported', valid: false,
          errorMessage: 'notification_unsupported_system', detail: `os='${platform.os}' arch='${platform.arch}'` }
      }
      if (!disconnected) {
        return { ...base, state: 'unavailable', valid: false,
          errorMessage: 'notification_no_backend', detail: probeError.message || String(probeError) }
      }
      try {
        probe = await this._waitForLocalStart(server, disconnected)
      } catch (error) {
        return { ...base, state: 'unavailable', valid: false,
          errorMessage: 'notification_no_backend', detail: error.message || String(error) }
      }
    }

    const currentVersion = this._compatibleVersion(probe.version)
    if (currentVersion !== requiredVersion) {
      const detail = currentVersion
        ? `Expected ${requiredVersion}, found ${currentVersion}`
        : (probe.versionError || 'The proxy did not report its version')
      return { ...base, state: 'incompatible', valid: false, versionMismatch: true,
        currentVersion, errorMessage: 'notification_wrong_backend_version', detail,
        canDownload: downloadSupported }
    }

    const proxy = {
      proxyHost: server.host,
      proxyPort: probe.port,
      caCertificate: server.origin + '/ca.crt'
    }
    return { ...base, state: 'ready', valid: true, currentVersion,
      versionMismatch: false, canDownload: false, detail: '', proxy }
  }

  async checkConnection () {
    await this.configuration()
    const revision = this._connectionRevision
    if (this._connectionCheck && this._connectionCheck.revision === revision)
      return this._connectionCheck.promise

    const server = { ...this._server }
    const pending = this._checkConnection(server).then(status => {
      if (revision !== this._connectionRevision) return this.checkConnection()
      this._proxy = status.valid ? status.proxy : null
      this._lastConnectionStatus = status
      for (const handler of this.onStatus) {
        try {
          Promise.resolve(handler(status)).catch(error => console.error(error))
        } catch (error) { console.error(error) }
      }
      return status
    })
    this._connectionCheck = { revision, promise: pending }
    const clear = () => {
      if (this._connectionCheck?.promise === pending) this._connectionCheck = null
    }
    pending.then(clear, clear)
    return pending
  }

  async ensureProxy () {
    const status = await this.checkConnection()
    if (!status.valid) throw new Error(status.detail || 'Hamster proxy is unavailable')
    return status.proxy
  }

  _archivePath (path) {
    const replacements = { '/': '╱', '\\': '╲', '<': '⟨', '>': '⟩', ':': '꞉', '"': 'ˮ', '|': '∣', '*': '∗', '?': '？' }
    return path.map(part => part.replace(/[\\/<>:"|*?]/g, c => replacements[c])).join('/')
  }

  async finishRecording (path) {
    await this.ensureProxy()
    await this._catalogUpdate.catch(() => {})
    if (!path) return
    for (;;) {
      const response = await this._control('finish-recording', { path: this._archivePathValue(path) })
      if (response.status !== 202) return
      await Utils.sleep(50)
    }
  }

  async finishAllRecordings () {
    await this.ensureProxy()
    await this._catalogUpdate.catch(() => {})
    for (;;) {
      const response = await this._control('finish-all-recordings')
      if (response.status !== 202) return
      await Utils.sleep(50)
    }
  }

  async finishUnlistedRecordings (activePaths) {
    await this.ensureProxy()
    await this._catalogUpdate.catch(() => {})
    const fields = { count: String(activePaths.length) }
    activePaths.forEach((path, index) => {
      fields[`path.${index}`] = this._archivePathValue(path)
    })
    for (;;) {
      const response = await this._control('finish-unlisted-recordings', fields, 30000)
      if (response.status !== 202) return
      await Utils.sleep(50)
    }
  }

  _archivePathValue (path) {
    return Array.isArray(path) ? this._archivePath(path) : String(path).replace(/\\/g, '/')
  }

  archivePath (path) { return this._archivePathValue(path) }

  async _recordingOptions () {
    return {
      serve: await Utils.getSetting('default-serve-mode', 'latest'),
      'allow-lossy-compression': await Utils.getSetting('allow-lossy-compression', false)
        ? 'true' : 'false'
    }
  }

  async _setRecordingOptions () {
    await this._control('set-recording-options', await this._recordingOptions())
  }

  async _syncBookmarkCatalog (bookmarks) {
    await this.ensureProxy()
    await this._setRecordingOptions()
    const fields = { count: String(bookmarks.length) }
    bookmarks.forEach((bookmark, index) => {
      fields[`url.${index}`] = new URL(bookmark.url).href
      fields[`path.${index}`] = this._archivePathValue(bookmark.path)
    })
    await this._control('replace-bookmarks', fields, 30000)
  }

  syncBookmarkCatalog (bookmarks) {
    const snapshot = bookmarks.map(bookmark => ({
      url: bookmark.url,
      path: Array.isArray(bookmark.path) ? bookmark.path.slice() : bookmark.path
    }))
    const update = this._catalogUpdate.catch(() => {}).then(
      () => this._syncBookmarkCatalog(snapshot))
    this._catalogUpdate = update
    return update
  }

  async _updateBookmarkCatalog (removedPaths, bookmarks) {
    await this.ensureProxy()
    await this._setRecordingOptions()
    const fields = {
      'removed-count': String(removedPaths.length),
      count: String(bookmarks.length)
    }
    removedPaths.forEach((path, index) => {
      fields[`removed.${index}`] = this._archivePathValue(path)
    })
    bookmarks.forEach((bookmark, index) => {
      fields[`url.${index}`] = new URL(bookmark.url).href
      fields[`path.${index}`] = this._archivePathValue(bookmark.path)
    })
    await this._control('update-bookmarks', fields, 30000)
  }

  updateBookmarkCatalog (removedPaths, bookmarks) {
    const removed = removedPaths.map(path => this._archivePathValue(path))
    const upserts = bookmarks.map(bookmark => ({
      url: bookmark.url,
      path: this._archivePathValue(bookmark.path)
    }))
    const update = this._catalogUpdate.catch(() => {}).then(
      () => this._updateBookmarkCatalog(removed, upserts))
    this._catalogUpdate = update
    return update
  }

  updateRecordingOptions () {
    const update = this._catalogUpdate.catch(() => {}).then(async () => {
      await this.ensureProxy()
      await this._setRecordingOptions()
    })
    this._catalogUpdate = update
    return update
  }

  async getFileSize (path) { return JSON.parse((await this._control('get-archive-size', { path: this._archivePath(path) })).text) }
  async moveFile (from, to) { return JSON.parse((await this._control('move-archive', { from: this._archivePath(from), to: this._archivePath(to) })).text) }
  async deleteFile (path, id) { return JSON.parse((await this._control('delete-archive', { path: this._archivePath(path), id })).text) }
  async undeleteFile (id) { return JSON.parse((await this._control('restore-archive', { id })).text) }
  async getFileListing (path) { return JSON.parse((await this._control('get-archive-files', { path: this._archivePath(path) })).text) }
  async getLibraryListing () { return JSON.parse((await this._control('get-archive-list')).text) }
  async rebuildSearchIndex () { return JSON.parse((await this._control('rebuild-search-index')).text) }
  async getIndexStatus () { return JSON.parse((await this._control('get-search-index-status')).text) }

  async executeSearch (query, forSearchPage) {
    const terms = query.trim().split(/\s+/).filter(Boolean)
    if (!terms.length) return { matches: [] }
    const fields = { query: terms.map(term => '"' + term.replace(/"/g, '""') + '"*').join(' AND ') }
    if (forSearchPage) Object.assign(fields, { highlight: 'true', snippetSize: '32', maxCount: '20' })
    const deadline = Date.now() + 30000
    // Wait for any newly committed archives to finish indexing before returning results.
    for (;;) {
      const response = JSON.parse((await this._control('execute-search', fields)).text)
      if (!response.indexing || Date.now() >= deadline) return response
      await Utils.sleep(100)
    }
  }
}

DEBUG_LOG_ASYNC_CALLS(Backend.prototype)
