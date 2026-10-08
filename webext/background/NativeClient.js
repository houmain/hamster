'use strict'
/* global browser */

class NativeClient {
  constructor (nativeClientId) {
    this._nativeClientId = nativeClientId
    this._connection = null
  }

  // Starts the native host once and returns a promise that resolves with an
  // Error when its messaging channel closes. The native protocol itself is
  // deliberately one-way: the only message is Hamster's command line.
  start (commandline) {
    if (this._connection) return this._connection.disconnected

    let finish
    const disconnected = new Promise(resolve => { finish = resolve })
    const port = browser.runtime.connectNative(this._nativeClientId)
    const connection = { port, disconnected, finish }
    this._connection = connection

    port.onDisconnect.addListener(() => {
      if (this._connection !== connection) return
      const message = browser.runtime.lastError && browser.runtime.lastError.message
      const error = new Error(message || 'Hamster native host disconnected')
      console.error(error.message)
      this._connection = null
      finish(error)
    })

    try {
      port.postMessage(commandline)
    } catch (error) {
      this._connection = null
      finish(error)
      try { port.disconnect() } catch {}
      throw error
    }
    return disconnected
  }

  disconnect () {
    const connection = this._connection
    if (!connection) return
    this._connection = null
    connection.finish(new Error('Hamster native host was stopped'))
    try { connection.port.disconnect() } catch {}
  }
}
